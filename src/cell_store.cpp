#include <fastxlsx/detail/cell_store.hpp>
#include "package_reader.hpp"

#include <fastxlsx/detail/xml.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <exception>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

bool operator<(const CellPosition& left, const CellPosition& right) noexcept
{
    if (left.row != right.row) {
        return left.row < right.row;
    }
    return left.column < right.column;
}

StyleId make_source_style_id(std::uint32_t value) noexcept
{
    return StyleId(value, 0);
}

namespace {

void validate_position(std::uint32_t row, std::uint32_t column)
{
    (void)cell_reference(row, column);
}

std::size_t record_memory_usage(const CellRecord& record) noexcept
{
    return sizeof(CellRecord) + record.text_value.capacity();
}

std::size_t entry_memory_usage(const CellPosition& position, const CellRecord& record) noexcept
{
    return sizeof(position) + record_memory_usage(record) + (sizeof(void*) * 3);
}

bool is_space(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool is_ascii_alpha(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_ascii_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

bool is_ascii_xml_name_start(char ch)
{
    return is_ascii_alpha(ch) || ch == '_' || ch == ':';
}

bool is_ascii_xml_name_char(char ch)
{
    return is_ascii_xml_name_start(ch) || is_ascii_digit(ch) || ch == '-'
        || ch == '.';
}

bool is_xml_encoding_name(std::string_view value)
{
    if (value.empty() || !is_ascii_alpha(value.front())) {
        return false;
    }
    for (const char ch : value) {
        if (!is_ascii_alpha(ch) && !is_ascii_digit(ch) && ch != '.' && ch != '_'
            && ch != '-') {
            return false;
        }
    }
    return true;
}

bool is_xml_declaration_tag(std::string_view raw_tag)
{
    if (raw_tag.size() <= 5 || raw_tag.substr(0, 5) != "<?xml") {
        return false;
    }

    const char after_target = raw_tag[5];
    return is_space(after_target) || after_target == '?';
}

bool ascii_equals_ignore_case(std::string_view value, std::string_view lower_case)
{
    if (value.size() != lower_case.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        char ch = value[index];
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
        if (ch != lower_case[index]) {
            return false;
        }
    }
    return true;
}

bool is_reserved_xml_processing_instruction_target(std::string_view raw_tag)
{
    if (raw_tag.size() < 4 || raw_tag.substr(0, 2) != "<?") {
        return false;
    }

    std::size_t position = 2;
    const std::size_t target_begin = position;
    while (position < raw_tag.size() && !is_space(raw_tag[position])
        && raw_tag[position] != '?' && raw_tag[position] != '>') {
        ++position;
    }

    const std::string_view target =
        raw_tag.substr(target_begin, position - target_begin);
    return ascii_equals_ignore_case(target, "xml");
}

[[noreturn]] void throw_malformed_shared_strings_xml_declaration()
{
    throw FastXlsxError(
        "CellStore sharedStrings loader found malformed XML declaration");
}

void skip_spaces(std::string_view value, std::size_t& position)
{
    while (position < value.size() && is_space(value[position])) {
        ++position;
    }
}

std::pair<std::string_view, std::string_view> parse_xml_declaration_attribute(
    std::string_view body, std::size_t& position)
{
    skip_spaces(body, position);
    if (position >= body.size()) {
        throw_malformed_shared_strings_xml_declaration();
    }

    const std::size_t name_begin = position;
    while (position < body.size() && !is_space(body[position])
        && body[position] != '=') {
        ++position;
    }
    if (position == name_begin) {
        throw_malformed_shared_strings_xml_declaration();
    }

    const std::string_view name = body.substr(name_begin, position - name_begin);
    skip_spaces(body, position);
    if (position >= body.size() || body[position] != '=') {
        throw_malformed_shared_strings_xml_declaration();
    }
    ++position;

    skip_spaces(body, position);
    if (position >= body.size() || (body[position] != '"' && body[position] != '\'')) {
        throw_malformed_shared_strings_xml_declaration();
    }

    const char quote = body[position];
    ++position;
    const std::size_t value_begin = position;
    while (position < body.size() && body[position] != quote) {
        ++position;
    }
    if (position >= body.size()) {
        throw_malformed_shared_strings_xml_declaration();
    }

    const std::string_view value = body.substr(value_begin, position - value_begin);
    ++position;
    return {name, value};
}

void validate_shared_strings_xml_declaration(std::string_view raw_tag)
{
    if (raw_tag.size() < 7 || raw_tag.substr(raw_tag.size() - 2) != "?>") {
        throw_malformed_shared_strings_xml_declaration();
    }
    if (raw_tag.substr(0, 5) != "<?xml" || !is_space(raw_tag[5])) {
        throw_malformed_shared_strings_xml_declaration();
    }

    const std::string_view body = raw_tag.substr(2, raw_tag.size() - 4);
    std::size_t position = 3;
    const auto [version_name, version_value] =
        parse_xml_declaration_attribute(body, position);
    if (version_name != "version") {
        throw_malformed_shared_strings_xml_declaration();
    }
    if (version_value != "1.0" && version_value != "1.1") {
        throw FastXlsxError(
            "CellStore sharedStrings loader found unsupported XML declaration version");
    }

    bool saw_encoding = false;
    bool saw_standalone = false;
    while (true) {
        skip_spaces(body, position);
        if (position >= body.size()) {
            return;
        }

        const auto [name, value] = parse_xml_declaration_attribute(body, position);
        if (name == "version") {
            throw_malformed_shared_strings_xml_declaration();
        }
        if (name == "encoding") {
            if (saw_encoding || saw_standalone || !is_xml_encoding_name(value)) {
                throw_malformed_shared_strings_xml_declaration();
            }
            saw_encoding = true;
            continue;
        }
        if (name == "standalone") {
            if (saw_standalone || (value != "yes" && value != "no")) {
                throw_malformed_shared_strings_xml_declaration();
            }
            saw_standalone = true;
            continue;
        }
        throw_malformed_shared_strings_xml_declaration();
    }
}

void validate_shared_strings_processing_instruction(std::string_view raw_tag)
{
    if (raw_tag.size() < 4 || raw_tag.substr(0, 2) != "<?"
        || raw_tag.substr(raw_tag.size() - 2) != "?>") {
        throw FastXlsxError(
            "CellStore sharedStrings loader found malformed processing instruction");
    }

    std::size_t position = 2;
    const std::size_t target_begin = position;
    while (position < raw_tag.size() && !is_space(raw_tag[position])
        && raw_tag[position] != '?' && raw_tag[position] != '>') {
        ++position;
    }

    if (position == target_begin) {
        throw FastXlsxError(
            "CellStore sharedStrings loader found malformed processing instruction");
    }
    if (static_cast<unsigned char>(raw_tag[target_begin]) < 0x80u
        && !is_ascii_xml_name_start(raw_tag[target_begin])) {
        throw FastXlsxError(
            "CellStore sharedStrings loader found malformed processing instruction");
    }
    for (std::size_t target_index = target_begin + 1; target_index < position;
         ++target_index) {
        if (static_cast<unsigned char>(raw_tag[target_index]) < 0x80u
            && !is_ascii_xml_name_char(raw_tag[target_index])) {
            throw FastXlsxError(
                "CellStore sharedStrings loader found malformed processing instruction");
        }
    }
    if (raw_tag[position] == '?') {
        if (position + 1 >= raw_tag.size() || raw_tag[position + 1] != '>') {
            throw FastXlsxError(
                "CellStore sharedStrings loader found malformed processing instruction");
        }
    } else if (!is_space(raw_tag[position])) {
        throw FastXlsxError(
            "CellStore sharedStrings loader found malformed processing instruction");
    }
}

void append_utf8(std::string& output, std::uint32_t code_point)
{
    if (code_point <= 0x7Fu) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFu) {
        output.push_back(static_cast<char>(0xC0u | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else if (code_point <= 0xFFFFu) {
        output.push_back(static_cast<char>(0xE0u | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else if (code_point <= 0x10FFFFu) {
        output.push_back(static_cast<char>(0xF0u | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 12) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else {
        throw FastXlsxError("CellStore worksheet loader found an invalid Unicode code point");
    }
}

std::uint32_t parse_xml_character_reference(std::string_view entity)
{
    if (entity.size() < 2 || entity.front() != '#') {
        throw FastXlsxError("CellStore worksheet loader found an unknown XML entity reference");
    }

    std::size_t offset = 1;
    int base = 10;
    if (offset < entity.size() && (entity[offset] == 'x' || entity[offset] == 'X')) {
        base = 16;
        ++offset;
    }
    if (offset == entity.size()) {
        throw FastXlsxError("CellStore worksheet loader found an invalid XML character reference");
    }

    std::uint32_t value = 0;
    for (; offset < entity.size(); ++offset) {
        const char ch = entity[offset];
        int digit = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = ch - 'a' + 10;
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = ch - 'A' + 10;
        }

        if (digit < 0 || digit >= base) {
            throw FastXlsxError(
                "CellStore worksheet loader found an invalid XML character reference");
        }
        if (value > (0x10FFFFu - static_cast<std::uint32_t>(digit))
                / static_cast<std::uint32_t>(base)) {
            throw FastXlsxError(
                "CellStore worksheet loader XML character reference exceeds Unicode range");
        }
        value = value * static_cast<std::uint32_t>(base) + static_cast<std::uint32_t>(digit);
    }

    return value;
}

std::string unescape_xml_text(std::string_view value)
{
    std::string output;
    output.reserve(value.size());

    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        const char ch = value[offset];
        if (ch != '&') {
            output.push_back(ch);
            continue;
        }

        const std::size_t semicolon = value.find(';', offset + 1);
        if (semicolon == std::string_view::npos) {
            throw FastXlsxError("CellStore worksheet loader found an unterminated XML entity");
        }
        const std::string_view entity = value.substr(offset + 1, semicolon - offset - 1);
        if (entity == "amp") {
            output.push_back('&');
        } else if (entity == "lt") {
            output.push_back('<');
        } else if (entity == "gt") {
            output.push_back('>');
        } else if (entity == "quot") {
            output.push_back('"');
        } else if (entity == "apos") {
            output.push_back('\'');
        } else {
            append_utf8(output, parse_xml_character_reference(entity));
        }
        offset = semicolon;
    }

    return output;
}

std::uint32_t uppercase_column_value(char ch)
{
    const char upper =
        static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return static_cast<std::uint32_t>(upper - 'A' + 1);
}

std::string_view trim(std::string_view value)
{
    while (!value.empty() && is_space(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && is_space(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

std::string_view tag_body(std::string_view raw_tag)
{
    if (raw_tag.size() < 2 || raw_tag.front() != '<' || raw_tag.back() != '>') {
        throw FastXlsxError("CellStore worksheet loader found invalid XML markup");
    }

    std::string_view body = raw_tag.substr(1, raw_tag.size() - 2);
    body = trim(body);
    if (!body.empty() && body.front() == '/') {
        body.remove_prefix(1);
        body = trim(body);
    }
    if (!body.empty() && body.back() == '/') {
        body.remove_suffix(1);
        body = trim(body);
    }
    return body;
}

std::string_view local_xml_name(std::string_view name)
{
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) {
        return name;
    }
    return name.substr(colon + 1);
}

std::size_t find_xml_tag_end(std::string_view xml, std::size_t open)
{
    char quote = '\0';
    for (std::size_t index = open + 1; index < xml.size(); ++index) {
        const char ch = xml[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '>') {
            return index;
        }
    }

    throw FastXlsxError("CellStore worksheet loader found a truncated XML tag");
}

std::optional<std::string_view> unqualified_attribute_value(
    std::string_view raw_tag, std::string_view attribute_name)
{
    std::string_view body = tag_body(raw_tag);
    std::optional<std::string_view> value;
    std::size_t position = 0;
    while (position < body.size() && !is_space(body[position])) {
        ++position;
    }

    while (position < body.size()) {
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] == '/' || body[position] == '?') {
            return value;
        }

        const std::size_t name_begin = position;
        while (position < body.size() && !is_space(body[position]) && body[position] != '='
            && body[position] != '/' && body[position] != '?') {
            ++position;
        }
        const std::string_view name = body.substr(name_begin, position - name_begin);

        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] != '=') {
            if (name == attribute_name) {
                throw FastXlsxError(
                    "CellStore worksheet loader found an attribute without a value");
            }
            continue;
        }
        ++position;
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || (body[position] != '"' && body[position] != '\'')) {
            throw FastXlsxError(
                "CellStore worksheet loader found an unquoted attribute value");
        }

        const char quote = body[position];
        ++position;
        const std::size_t value_begin = position;
        while (position < body.size() && body[position] != quote) {
            ++position;
        }
        if (position >= body.size()) {
            throw FastXlsxError(
                "CellStore worksheet loader found an unterminated attribute value");
        }

        if (name == attribute_name) {
            if (value.has_value()) {
                throw FastXlsxError(
                    "CellStore worksheet loader found duplicate attributes");
            }
            value = body.substr(value_begin, position - value_begin);
        }
        ++position;
    }

    return value;
}

void validate_raw_tag_attribute_syntax(
    std::string_view raw_tag, std::string_view context)
{
    std::string_view body = tag_body(raw_tag);
    std::size_t position = 0;
    while (position < body.size() && !is_space(body[position]) && body[position] != '/'
        && body[position] != '?') {
        ++position;
    }

    while (position < body.size()) {
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] == '/' || body[position] == '?') {
            return;
        }

        const std::size_t name_begin = position;
        while (position < body.size() && !is_space(body[position]) && body[position] != '='
            && body[position] != '/' && body[position] != '?') {
            ++position;
        }
        if (position == name_begin) {
            throw FastXlsxError(std::string(context) + " found an invalid attribute name");
        }

        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] != '=') {
            throw FastXlsxError(std::string(context) + " found an attribute without a value");
        }
        ++position;

        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || (body[position] != '"' && body[position] != '\'')) {
            throw FastXlsxError(std::string(context) + " found an unquoted attribute value");
        }

        const char quote = body[position];
        ++position;
        while (position < body.size() && body[position] != quote) {
            ++position;
        }
        if (position >= body.size()) {
            throw FastXlsxError(std::string(context) + " found an unterminated attribute value");
        }
        ++position;
    }
}

bool has_non_whitespace(std::string_view value)
{
    for (const char ch : value) {
        if (!is_space(ch)) {
            return true;
        }
    }
    return false;
}

bool is_self_closing_raw_tag(std::string_view raw_tag)
{
    std::string_view body = raw_tag.substr(1, raw_tag.size() - 2);
    body = trim(body);
    if (body.empty()) {
        return false;
    }
    std::size_t end = body.size();
    while (end > 0 && is_space(body[end - 1])) {
        --end;
    }
    return end > 0 && body[end - 1] == '/';
}

std::string_view raw_tag_name(std::string_view raw_tag)
{
    std::string_view body = tag_body(raw_tag);
    std::size_t end = 0;
    while (end < body.size() && !is_space(body[end]) && body[end] != '/' && body[end] != '?') {
        ++end;
    }
    return local_xml_name(body.substr(0, end));
}

int hex_digit_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

std::string decode_percent_encoded_relationship_target(std::string_view target)
{
    std::string decoded;
    decoded.reserve(target.size());

    for (std::size_t index = 0; index < target.size(); ++index) {
        const char ch = target[index];
        if (ch != '%') {
            decoded.push_back(ch);
            continue;
        }

        if (index + 2 >= target.size()) {
            throw FastXlsxError("relationship target percent escape is incomplete");
        }

        const int high = hex_digit_value(target[index + 1]);
        const int low = hex_digit_value(target[index + 2]);
        if (high < 0 || low < 0) {
            throw FastXlsxError("relationship target percent escape is invalid");
        }

        const char decoded_char = static_cast<char>((high << 4) | low);
        if (decoded_char == '\0') {
            throw FastXlsxError("relationship target cannot contain null bytes");
        }

        decoded.push_back(decoded_char);
        index += 2;
    }

    return decoded;
}

std::string resolve_internal_relationship_target_path(
    const PartName& source_part, const Relationship& relationship)
{
    if (relationship.target_mode == Relationship::TargetMode::External) {
        throw FastXlsxError("sharedStrings relationship target cannot be external");
    }
    if (relationship.target.find_first_of("?#") != std::string::npos) {
        throw FastXlsxError("sharedStrings relationship target must be a package part");
    }

    const std::string decoded_target =
        decode_percent_encoded_relationship_target(relationship.target);
    if (!decoded_target.empty() && decoded_target.front() == '/') {
        return PartName(decoded_target).value();
    }

    const std::string& source = source_part.value();
    const std::size_t slash = source.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return PartName("/" + decoded_target).value();
    }
    return PartName(source.substr(0, slash) + "/" + decoded_target).value();
}

std::vector<std::string> parse_shared_strings_xml(std::string_view xml)
{
    std::vector<std::string> strings;
    std::vector<std::string> element_stack;
    std::string current_text;
    bool saw_root = false;
    bool closed_root = false;
    bool saw_xml_declaration = false;
    bool saw_prolog_trivia = false;
    bool saw_prolog_text = false;
    bool current_item_saw_direct_text = false;
    bool current_item_saw_rich_run = false;

    const auto stack_contains = [&](std::string_view name) {
        return std::find(element_stack.begin(), element_stack.end(), name)
            != element_stack.end();
    };

    const auto inside_ignored_metadata = [&]() {
        return stack_contains("rPh") || stack_contains("phoneticPr")
            || stack_contains("extLst");
    };

    const auto inside_text_element = [&]() {
        return !element_stack.empty() && element_stack.back() == "t";
    };

    const auto capture_text = [&](std::string_view text) {
        if (text.empty()) {
            return;
        }
        if (inside_ignored_metadata()) {
            return;
        }
        if (!element_stack.empty() && element_stack.back() == "t") {
            current_text += unescape_xml_text(text);
            return;
        }
        if (!saw_root) {
            saw_prolog_text = true;
        }
        if (has_non_whitespace(text)) {
            throw FastXlsxError("CellStore sharedStrings loader found text outside a text element");
        }
    };

    for (std::size_t offset = 0;;) {
        const std::size_t open = xml.find('<', offset);
        if (open == std::string_view::npos) {
            capture_text(xml.substr(offset));
            break;
        }

        capture_text(xml.substr(offset, open - offset));
        if (open + 1 >= xml.size()) {
            throw FastXlsxError("CellStore sharedStrings loader found a truncated XML tag");
        }

        if (xml.substr(open, 4) == "<!--") {
            if (inside_text_element()) {
                throw FastXlsxError(
                    "CellStore sharedStrings loader found nested markup inside a text element");
            }
            if (!saw_root) {
                saw_prolog_trivia = true;
            }
            const std::size_t close = xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError("CellStore sharedStrings loader found an unterminated comment");
            }
            offset = close + 3;
            continue;
        }

        const char marker = xml[open + 1];
        const std::size_t close = find_xml_tag_end(xml, open);
        const std::string_view raw_tag = xml.substr(open, close - open + 1);

        if (marker == '?') {
            if (inside_text_element()) {
                throw FastXlsxError(
                    "CellStore sharedStrings loader found nested markup inside a text element");
            }
            if (is_xml_declaration_tag(raw_tag)) {
                if (saw_root) {
                    throw FastXlsxError(
                        "CellStore sharedStrings loader found XML declaration after sharedStrings root start");
                }
                if (saw_xml_declaration) {
                    throw FastXlsxError(
                        "CellStore sharedStrings loader found duplicate XML declaration");
                }
                if (saw_prolog_trivia) {
                    throw FastXlsxError(
                        "CellStore sharedStrings loader found XML declaration after sharedStrings prolog markup");
                }
                if (saw_prolog_text) {
                    throw FastXlsxError(
                        "CellStore sharedStrings loader found XML declaration after sharedStrings prolog text");
                }
                validate_shared_strings_xml_declaration(raw_tag);
                saw_xml_declaration = true;
            } else {
                validate_shared_strings_processing_instruction(raw_tag);
                if (is_reserved_xml_processing_instruction_target(raw_tag)) {
                    throw FastXlsxError(
                        "CellStore sharedStrings loader found reserved XML processing instruction target");
                }
                if (!saw_root) {
                    saw_prolog_trivia = true;
                }
            }
            offset = close + 1;
            continue;
        }

        if (marker == '!') {
            if (inside_text_element()) {
                throw FastXlsxError(
                    "CellStore sharedStrings loader found nested markup inside a text element");
            }
            throw FastXlsxError(
                "CellStore sharedStrings loader found unsupported markup declaration");
        }

        validate_raw_tag_attribute_syntax(raw_tag, "CellStore sharedStrings loader");
        const std::string_view name = raw_tag_name(raw_tag);
        const bool closing = marker == '/';
        const bool self_closing = is_self_closing_raw_tag(raw_tag);

        if (closing) {
            if (element_stack.empty() || element_stack.back() != name) {
                throw FastXlsxError(
                    "CellStore sharedStrings loader found mismatched closing tags");
            }
            if (name == "si") {
                strings.push_back(current_text);
                current_text.clear();
                current_item_saw_direct_text = false;
                current_item_saw_rich_run = false;
            }
            element_stack.pop_back();
            if (element_stack.empty()) {
                closed_root = true;
            }
            offset = close + 1;
            continue;
        }

        if (closed_root) {
            throw FastXlsxError("CellStore sharedStrings loader found markup after the root");
        }

        if (!saw_root) {
            if (name != "sst") {
                throw FastXlsxError(
                    "CellStore sharedStrings loader root is missing an sst element");
            }
            saw_root = true;
            if (!self_closing) {
                element_stack.push_back(std::string(name));
            } else {
                closed_root = true;
            }
            offset = close + 1;
            continue;
        }

        if (name == "si"
            && !(element_stack.size() == 1 && element_stack.back() == "sst")) {
            throw FastXlsxError(
                "CellStore sharedStrings loader found a nested shared string item");
        }
        if (!element_stack.empty() && element_stack.back() == "t") {
            throw FastXlsxError(
                "CellStore sharedStrings loader found nested markup inside a text element");
        }
        if (!inside_ignored_metadata() && !stack_contains("rPr")) {
            const std::string_view parent =
                element_stack.empty() ? std::string_view {} : element_stack.back();
            if (parent == "sst" && name != "si" && name != "extLst"
                && name != "phoneticPr") {
                throw FastXlsxError(
                    "CellStore sharedStrings loader found an unsupported root child element");
            }
            if (parent == "si" && name == "rPr") {
                throw FastXlsxError(
                    "CellStore sharedStrings loader found malformed rich text metadata");
            }
            if (parent == "si" && name == "t") {
                if (current_item_saw_rich_run) {
                    throw FastXlsxError(
                        "CellStore sharedStrings loader found mixed direct and rich text in a shared string item");
                }
                current_item_saw_direct_text = true;
            }
            if (parent == "si" && name == "r") {
                if (current_item_saw_direct_text) {
                    throw FastXlsxError(
                        "CellStore sharedStrings loader found mixed direct and rich text in a shared string item");
                }
                current_item_saw_rich_run = true;
            }
            if (parent == "si" && name != "t" && name != "r" && name != "rPh"
                && name != "phoneticPr" && name != "extLst") {
                throw FastXlsxError(
                    "CellStore sharedStrings loader found an unsupported shared string item element");
            }
            if (parent == "r" && name != "t" && name != "rPr") {
                throw FastXlsxError(
                    "CellStore sharedStrings loader found an unsupported shared string rich text element");
            }
        } else if (stack_contains("rPr") && name == "t") {
            throw FastXlsxError(
                "CellStore sharedStrings loader found malformed rich text metadata");
        }

        if (!self_closing) {
            element_stack.push_back(std::string(name));
        } else if (name == "si") {
            strings.push_back(current_text);
            current_text.clear();
            current_item_saw_direct_text = false;
            current_item_saw_rich_run = false;
        }

        offset = close + 1;
    }

    if (!saw_root || !element_stack.empty()) {
        throw FastXlsxError("CellStore sharedStrings loader found malformed XML");
    }

    return strings;
}

std::optional<std::vector<std::string>> load_shared_strings_from_workbook(
    const PackageReader& reader)
{
    const PartName workbook_part = reader.workbook_part();
    const RelationshipSet* workbook_relationships = reader.relationships_for(workbook_part);
    if (workbook_relationships == nullptr) {
        return std::nullopt;
    }

    const Relationship* shared_strings_relationship = nullptr;
    for (const Relationship& relationship : workbook_relationships->relationships()) {
        if (relationship.type !=
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings") {
            continue;
        }
        if (shared_strings_relationship != nullptr) {
            throw FastXlsxError(
                "workbook sharedStrings lookup found multiple sharedStrings relationships");
        }
        shared_strings_relationship = &relationship;
    }

    if (shared_strings_relationship == nullptr) {
        return std::nullopt;
    }

    const PartName shared_strings_part(resolve_internal_relationship_target_path(
        workbook_part, *shared_strings_relationship));
    const PackagePart* package_part = reader.part_index().find_part(shared_strings_part);
    if (package_part == nullptr) {
        throw FastXlsxError(
            "workbook sharedStrings relationship targets an unknown package part");
    }
    if (package_part->content_type
        != "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml") {
        throw FastXlsxError(
            "workbook sharedStrings relationship target is not a sharedStrings part");
    }

    try {
        return parse_shared_strings_xml(reader.read_entry(shared_strings_part.zip_path()));
    } catch (const std::exception& error) {
        throw FastXlsxError("failed to load workbook sharedStrings part '"
            + shared_strings_part.value() + "' ZIP entry '" + shared_strings_part.zip_path()
            + "': " + error.what());
    }
}

bool raw_tag_has_attributes(std::string_view raw_tag)
{
    const std::string_view body = tag_body(raw_tag);
    std::size_t position = 0;
    while (position < body.size() && !is_space(body[position])) {
        ++position;
    }
    while (position < body.size() && is_space(body[position])) {
        ++position;
    }
    return position < body.size() && body[position] != '/' && body[position] != '?';
}

bool is_allowed_attribute_name(
    std::string_view name,
    std::initializer_list<std::string_view> allowed_names)
{
    return std::find(allowed_names.begin(), allowed_names.end(), name) != allowed_names.end();
}

bool raw_tag_has_unsupported_attributes(
    std::string_view raw_tag, std::initializer_list<std::string_view> allowed_names)
{
    std::string_view body = tag_body(raw_tag);
    std::size_t position = 0;
    while (position < body.size() && !is_space(body[position])) {
        ++position;
    }

    while (position < body.size()) {
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] == '/' || body[position] == '?') {
            return false;
        }

        const std::size_t name_begin = position;
        while (position < body.size() && !is_space(body[position]) && body[position] != '='
            && body[position] != '/' && body[position] != '?') {
            ++position;
        }
        const std::string_view name = body.substr(name_begin, position - name_begin);
        if (!is_allowed_attribute_name(name, allowed_names)) {
            return true;
        }

        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] != '=') {
            return true;
        }
        ++position;
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || (body[position] != '"' && body[position] != '\'')) {
            throw FastXlsxError(
                "CellStore worksheet loader found an unquoted attribute value");
        }

        const char quote = body[position];
        ++position;
        while (position < body.size() && body[position] != quote) {
            ++position;
        }
        if (position >= body.size()) {
            throw FastXlsxError(
                "CellStore worksheet loader found an unterminated attribute value");
        }
        ++position;
    }

    return false;
}

bool raw_tag_has_unsupported_attributes(
    std::string_view raw_tag,
    std::string_view allowed_first,
    std::optional<std::string_view> allowed_second = std::nullopt,
    std::optional<std::string_view> allowed_third = std::nullopt)
{
    if (allowed_second.has_value() && allowed_third.has_value()) {
        return raw_tag_has_unsupported_attributes(
            raw_tag, {allowed_first, *allowed_second, *allowed_third});
    }
    if (allowed_second.has_value()) {
        return raw_tag_has_unsupported_attributes(raw_tag, {allowed_first, *allowed_second});
    }
    return raw_tag_has_unsupported_attributes(raw_tag, {allowed_first});
}

std::optional<StyleId> parse_source_style_attribute_for_load(std::string_view raw_tag)
{
    const std::optional<std::string_view> style_id =
        unqualified_attribute_value(raw_tag, "s");
    if (!style_id.has_value()) {
        return std::nullopt;
    }

    std::uint64_t parsed_style_id = 0;
    const char* const begin = style_id->data();
    const char* const end = begin + style_id->size();
    const auto result = std::from_chars(begin, end, parsed_style_id);
    if (style_id->empty() || result.ec != std::errc {} || result.ptr != end
        || parsed_style_id > std::numeric_limits<std::uint32_t>::max()
        || (style_id->size() > 1 && style_id->front() == '0')) {
        throw FastXlsxError(
            "CellStore worksheet loader found an invalid style id reference");
    }

    if (parsed_style_id == 0) {
        return std::nullopt;
    }
    return make_source_style_id(static_cast<std::uint32_t>(parsed_style_id));
}

struct ParsedCellReference {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

std::uint32_t parse_row_number(std::string_view value, std::string_view context)
{
    if (value.empty()) {
        throw FastXlsxError(std::string(context) + " requires a row number");
    }

    std::uint64_t row = 0;
    for (const char ch : value) {
        if (!is_ascii_digit(ch)) {
            throw FastXlsxError(std::string(context) + " found an invalid row number");
        }
        row = row * 10U + static_cast<std::uint32_t>(ch - '0');
        if (row > 1048576U) {
            throw FastXlsxError(std::string(context) + " row exceeds Excel limits");
        }
    }
    if (row == 0) {
        throw FastXlsxError(std::string(context) + " row must be one-based");
    }
    return static_cast<std::uint32_t>(row);
}

ParsedCellReference parse_cell_reference_for_load(std::string_view reference)
{
    if (reference.empty()) {
        throw FastXlsxError("CellStore worksheet loader requires explicit cell references");
    }

    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < reference.size() && is_ascii_alpha(reference[position])) {
        column = column * 26U + uppercase_column_value(reference[position]);
        if (column > 16384U) {
            throw FastXlsxError(
                "CellStore worksheet loader cell column exceeds Excel limits");
        }
        ++position;
    }
    if (position == 0 || position >= reference.size()) {
        throw FastXlsxError("CellStore worksheet loader found an invalid cell reference");
    }

    std::uint64_t row = 0;
    while (position < reference.size() && is_ascii_digit(reference[position])) {
        row = row * 10U + static_cast<std::uint32_t>(reference[position] - '0');
        if (row > 1048576U) {
            throw FastXlsxError(
                "CellStore worksheet loader cell row exceeds Excel limits");
        }
        ++position;
    }
    if (position != reference.size() || row == 0 || column == 0) {
        throw FastXlsxError("CellStore worksheet loader found an invalid cell reference");
    }

    return ParsedCellReference {
        static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)};
}

struct FormulaReferenceToken {
    std::size_t length = 0;
    std::uint32_t row = 0;
    std::uint32_t column = 0;
    bool row_absolute = false;
    bool column_absolute = false;
};

bool is_formula_name_char(char ch)
{
    return is_ascii_alpha(ch) || is_ascii_digit(ch) || ch == '_' || ch == '.';
}

bool has_formula_reference_left_boundary(std::string_view formula, std::size_t position)
{
    if (position == 0) {
        return true;
    }
    return !is_formula_name_char(formula[position - 1]);
}

bool has_formula_reference_right_boundary(std::string_view formula, std::size_t position)
{
    if (position >= formula.size()) {
        return true;
    }
    const char next = formula[position];
    return !is_formula_name_char(next) && next != '(' && next != '!';
}

std::optional<FormulaReferenceToken> parse_formula_reference_token(
    std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    if (!has_formula_reference_left_boundary(formula, start)) {
        return std::nullopt;
    }

    bool column_absolute = false;
    if (position < formula.size() && formula[position] == '$') {
        column_absolute = true;
        ++position;
    }

    std::uint64_t column = 0;
    std::size_t column_letters = 0;
    while (position < formula.size() && is_ascii_alpha(formula[position])) {
        column = column * 26U + uppercase_column_value(formula[position]);
        ++column_letters;
        ++position;
    }
    if (column_letters == 0 || column == 0 || column > 16384U) {
        return std::nullopt;
    }

    bool row_absolute = false;
    if (position < formula.size() && formula[position] == '$') {
        row_absolute = true;
        ++position;
    }

    const std::size_t row_begin = position;
    std::uint64_t row = 0;
    while (position < formula.size() && is_ascii_digit(formula[position])) {
        row = row * 10U + static_cast<std::uint32_t>(formula[position] - '0');
        ++position;
    }
    if (position == row_begin || row == 0 || row > 1048576U) {
        return std::nullopt;
    }
    if (!has_formula_reference_right_boundary(formula, position)) {
        return std::nullopt;
    }

    return FormulaReferenceToken {position - start, static_cast<std::uint32_t>(row),
        static_cast<std::uint32_t>(column), row_absolute, column_absolute};
}

void append_formula_column_reference(std::string& output, std::uint32_t column)
{
    char letters[3] {};
    std::size_t count = 0;
    while (column > 0) {
        --column;
        letters[count] = static_cast<char>('A' + (column % 26U));
        ++count;
        column /= 26U;
    }
    while (count > 0) {
        --count;
        output += letters[count];
    }
}

std::optional<std::uint32_t> translate_formula_axis(
    std::uint32_t value, std::int64_t delta, bool absolute, std::uint32_t limit)
{
    if (absolute) {
        return value;
    }
    const std::int64_t translated = static_cast<std::int64_t>(value) + delta;
    if (translated < 1 || translated > static_cast<std::int64_t>(limit)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(translated);
}

void append_translated_formula_reference(
    std::string& output,
    const FormulaReferenceToken& token,
    std::int64_t row_delta,
    std::int64_t column_delta)
{
    const std::optional<std::uint32_t> translated_column = translate_formula_axis(
        token.column, column_delta, token.column_absolute, 16384U);
    const std::optional<std::uint32_t> translated_row = translate_formula_axis(
        token.row, row_delta, token.row_absolute, 1048576U);
    if (!translated_column.has_value() || !translated_row.has_value()) {
        output += "#REF!";
        return;
    }

    if (token.column_absolute) {
        output += '$';
    }
    append_formula_column_reference(output, *translated_column);
    if (token.row_absolute) {
        output += '$';
    }
    append_unsigned_decimal(output, *translated_row);
}

void append_quoted_formula_string(
    std::string& output, std::string_view formula, std::size_t& position)
{
    output += formula[position++];
    while (position < formula.size()) {
        output += formula[position];
        if (formula[position] == '"') {
            ++position;
            if (position < formula.size() && formula[position] == '"') {
                output += formula[position++];
                continue;
            }
            return;
        }
        ++position;
    }
}

void append_quoted_sheet_name(
    std::string& output, std::string_view formula, std::size_t& position)
{
    output += formula[position++];
    while (position < formula.size()) {
        output += formula[position];
        if (formula[position] == '\'') {
            ++position;
            if (position < formula.size() && formula[position] == '\'') {
                output += formula[position++];
                continue;
            }
            return;
        }
        ++position;
    }
}

void append_bracketed_formula_token(
    std::string& output, std::string_view formula, std::size_t& position)
{
    output += formula[position++];
    while (position < formula.size()) {
        output += formula[position];
        const bool closed = formula[position] == ']';
        ++position;
        if (closed) {
            return;
        }
    }
}

std::string translate_shared_formula_text(
    std::string_view formula, CellPosition base, CellPosition target)
{
    const std::int64_t row_delta =
        static_cast<std::int64_t>(target.row) - static_cast<std::int64_t>(base.row);
    const std::int64_t column_delta =
        static_cast<std::int64_t>(target.column) - static_cast<std::int64_t>(base.column);
    if (row_delta == 0 && column_delta == 0) {
        return std::string(formula);
    }

    std::string translated;
    translated.reserve(formula.size());
    std::size_t position = 0;
    while (position < formula.size()) {
        if (formula[position] == '"') {
            append_quoted_formula_string(translated, formula, position);
            continue;
        }
        if (formula[position] == '\'') {
            append_quoted_sheet_name(translated, formula, position);
            continue;
        }
        if (formula[position] == '[') {
            append_bracketed_formula_token(translated, formula, position);
            continue;
        }

        const std::optional<FormulaReferenceToken> token =
            parse_formula_reference_token(formula, position);
        if (token.has_value()) {
            append_translated_formula_reference(
                translated, *token, row_delta, column_delta);
            position += token->length;
            continue;
        }

        translated += formula[position++];
    }
    return translated;
}

bool needs_space_preserve(std::string_view value)
{
    return !value.empty()
        && (value.front() == ' ' || value.front() == '\t' || value.front() == '\n'
            || value.front() == '\r' || value.back() == ' ' || value.back() == '\t'
            || value.back() == '\n' || value.back() == '\r');
}

void append_text_element(std::string& xml, std::string_view value)
{
    if (needs_space_preserve(value)) {
        xml += "<t xml:space=\"preserve\">";
    } else {
        xml += "<t>";
    }
    append_escaped_xml_text(xml, value);
    xml += "</t>";
}

void append_style_attribute(std::string& xml, const CellRecord& record)
{
    if (!record.style_id.has_value() || record.style_id->value() == 0) {
        return;
    }

    xml += " s=\"";
    append_unsigned_decimal(xml, record.style_id->value());
    xml += "\"";
}

void append_cell_xml(std::string& xml, const CellPosition& position, const CellRecord& record)
{
    xml += "<c r=\"";
    append_cell_reference(xml, position.row, position.column);
    xml += "\"";
    append_style_attribute(xml, record);

    switch (record.kind) {
    case CellValueKind::Blank:
        xml += "/>";
        break;
    case CellValueKind::Number:
        xml += "><v>";
        append_number(xml, record.number_value);
        xml += "</v></c>";
        break;
    case CellValueKind::Text:
        xml += " t=\"inlineStr\"><is>";
        append_text_element(xml, record.text_value);
        xml += "</is></c>";
        break;
    case CellValueKind::Boolean:
        xml += " t=\"b\"><v>";
        xml += record.boolean_value ? "1" : "0";
        xml += "</v></c>";
        break;
    case CellValueKind::Formula:
        xml += "><f>";
        append_escaped_xml_text(xml, record.text_value);
        xml += "</f></c>";
        break;
    }
}

class CellStoreSheetDataChunkSourceState {
public:
    explicit CellStoreSheetDataChunkSourceState(const CellStore& store)
        : current_(store.records().begin())
        , end_(store.records().end())
    {
    }

    bool read(std::string& chunk)
    {
        chunk.clear();
        if (!emitted_start_) {
            emitted_start_ = true;
            chunk = "<sheetData>";
            return true;
        }

        if (current_ != end_) {
            if (!row_open_) {
                return emit_row_start(chunk);
            }
            if (current_->first.row != current_row_) {
                row_open_ = false;
                chunk = "</row>";
                return true;
            }

            append_cell_xml(chunk, current_->first, current_->second);
            ++current_;
            return true;
        }

        if (row_open_) {
            row_open_ = false;
            chunk = "</row>";
            return true;
        }

        if (!emitted_end_) {
            emitted_end_ = true;
            chunk = "</sheetData>";
            return true;
        }

        return false;
    }

private:
    using RecordIterator = std::map<CellPosition, CellRecord>::const_iterator;

    bool emit_row_start(std::string& chunk)
    {
        current_row_ = current_->first.row;
        chunk = "<row r=\"";
        append_unsigned_decimal(chunk, current_row_);
        chunk += "\">";
        row_open_ = true;
        return true;
    }

    RecordIterator current_;
    RecordIterator end_;
    std::uint32_t current_row_ = 0;
    bool emitted_start_ = false;
    bool emitted_end_ = false;
    bool row_open_ = false;
};

class CellStoreWorksheetChunkSourceState {
public:
    explicit CellStoreWorksheetChunkSourceState(const CellStore& store)
        : sheet_data_source_(cell_store_sheet_data_chunk_source(store))
        , dimension_reference_(cell_store_dimension_reference(store))
    {
    }

    bool read(std::string& chunk)
    {
        chunk.clear();
        switch (phase_) {
        case Phase::XmlDeclaration:
            phase_ = Phase::WorksheetStart;
            chunk = R"(<?xml version="1.0" encoding="UTF-8"?>)";
            return true;
        case Phase::WorksheetStart:
            phase_ = Phase::Dimension;
            chunk = R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)";
            return true;
        case Phase::Dimension:
            phase_ = Phase::SheetData;
            chunk = R"(<dimension ref=")";
            chunk += dimension_reference_;
            chunk += R"("/>)";
            return true;
        case Phase::SheetData:
            if (sheet_data_source_(chunk)) {
                return true;
            }
            phase_ = Phase::WorksheetEnd;
            [[fallthrough]];
        case Phase::WorksheetEnd:
            phase_ = Phase::Done;
            chunk = "</worksheet>";
            return true;
        case Phase::Done:
            return false;
        }

        return false;
    }

private:
    enum class Phase {
        XmlDeclaration,
        WorksheetStart,
        Dimension,
        SheetData,
        WorksheetEnd,
        Done,
    };

    WorksheetInputChunkCallback sheet_data_source_;
    std::string dimension_reference_;
    Phase phase_ = Phase::XmlDeclaration;
};

enum class SourceCellType {
    Number,
    String,
    Boolean,
    InlineString,
    SharedString,
};

SourceCellType source_cell_type_from_raw_tag(
    std::string_view raw_xml, bool allow_shared_strings)
{
    const std::optional<std::string_view> type = unqualified_attribute_value(raw_xml, "t");
    if (!type.has_value() || *type == "n") {
        return SourceCellType::Number;
    }
    if (*type == "str") {
        return SourceCellType::String;
    }
    if (*type == "b") {
        return SourceCellType::Boolean;
    }
    if (*type == "inlineStr") {
        return SourceCellType::InlineString;
    }
    if (*type == "s") {
        if (!allow_shared_strings) {
            throw FastXlsxError(
                "CellStore worksheet loader does not load shared string indexes");
        }
        return SourceCellType::SharedString;
    }

    std::string message = "CellStore worksheet loader found unsupported cell type";
    if (!type->empty()) {
        message += ": ";
        message += std::string(*type);
    }
    throw FastXlsxError(message);
}

bool is_closing_raw_tag(std::string_view raw_xml)
{
    return raw_xml.size() > 2 && raw_xml.front() == '<' && raw_xml[1] == '/';
}

struct ActiveSourceCell {
    CellPosition position;
    SourceCellType type = SourceCellType::Number;
    std::optional<StyleId> style_id;
    std::string current_value_element;
    std::string scalar_text;
    std::string inline_text;
    std::string formula_text;
    std::optional<std::uint64_t> shared_formula_index;
    std::size_t inline_rich_run_depth = 0;
    std::size_t inline_rich_properties_depth = 0;
    std::size_t inline_ignored_metadata_depth = 0;
    std::size_t inline_ignored_metadata_text_depth = 0;
    std::vector<std::string> inline_ignored_metadata_stack;
    bool saw_scalar_value_element = false;
    bool saw_inline_text_element = false;
    bool saw_direct_inline_text_element = false;
    bool saw_formula_element = false;
    bool saw_formula_metadata_attributes = false;
    bool formula_type_is_shared = false;
};

using SharedStringsProvider = std::function<const std::vector<std::string>&()>;

struct SharedFormulaDefinition {
    CellPosition base;
    std::string formula_text;
};

using SharedFormulaDefinitions = std::map<std::uint64_t, SharedFormulaDefinition>;

bool parse_shared_string_index(
    std::string_view value, std::uint64_t& index, const char* error_message)
{
    if (value.empty()) {
        throw FastXlsxError(error_message);
    }

    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto result = std::from_chars(begin, end, index);
    return result.ec == std::errc {} && result.ptr == end;
}

double parse_cell_number(std::string_view value)
{
    double number = 0.0;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto result = std::from_chars(begin, end, number);
    if (result.ec != std::errc {} || result.ptr != end || !std::isfinite(number)) {
        throw FastXlsxError("CellStore worksheet loader found an invalid numeric cell value");
    }
    return number;
}

bool parse_cell_boolean(std::string_view value)
{
    if (value == "1") {
        return true;
    }
    if (value == "0") {
        return false;
    }
    throw FastXlsxError("CellStore worksheet loader found an invalid boolean cell value");
}

CellValue materialize_cell_value(
    const ActiveSourceCell& cell,
    const SharedStringsProvider* shared_strings_provider,
    const SharedFormulaDefinitions& shared_formula_definitions)
{
    CellValue value = CellValue::blank();
    if (cell.saw_formula_element) {
        if (!cell.formula_text.empty()) {
            value = CellValue::formula(cell.formula_text);
            if (cell.style_id.has_value()) {
                value = value.with_style(*cell.style_id);
            }
            return value;
        }
        if (cell.formula_type_is_shared && cell.shared_formula_index.has_value()) {
            const auto definition =
                shared_formula_definitions.find(*cell.shared_formula_index);
            if (definition != shared_formula_definitions.end()) {
                value = CellValue::formula(translate_shared_formula_text(
                    definition->second.formula_text, definition->second.base,
                    cell.position));
                if (cell.style_id.has_value()) {
                    value = value.with_style(*cell.style_id);
                }
                return value;
            }
        }
        if (!cell.saw_formula_metadata_attributes) {
            throw FastXlsxError("CellStore worksheet loader found an empty formula text");
        }
        // Shared/array formula metadata is not preserved by CellStore. If a
        // source cell cannot be resolved through a source-order shared formula
        // definition and carries a cached value, materialize that scalar value
        // instead of inventing formula text.
    }

    switch (cell.type) {
    case SourceCellType::Number:
        if (!cell.saw_scalar_value_element || cell.scalar_text.empty()) {
            break;
        }
        value = CellValue::number(parse_cell_number(cell.scalar_text));
        break;
    case SourceCellType::String:
        if (!cell.saw_scalar_value_element) {
            break;
        }
        value = CellValue::text(cell.scalar_text);
        break;
    case SourceCellType::Boolean:
        if (!cell.saw_scalar_value_element || cell.scalar_text.empty()) {
            break;
        }
        value = CellValue::boolean(parse_cell_boolean(cell.scalar_text));
        break;
    case SourceCellType::InlineString:
        if (!cell.saw_inline_text_element) {
            break;
        }
        value = CellValue::text(cell.inline_text);
        break;
    case SourceCellType::SharedString:
        if (!cell.saw_scalar_value_element || cell.scalar_text.empty()) {
            throw FastXlsxError(
                "CellStore worksheet loader found an invalid shared string index");
        }
        if (shared_strings_provider == nullptr) {
            throw FastXlsxError(
                "CellStore worksheet loader found shared string indexes without a sharedStrings table");
        }
        {
            const std::vector<std::string>& shared_strings = (*shared_strings_provider)();
            std::uint64_t index = 0;
            if (!parse_shared_string_index(
                    cell.scalar_text, index,
                    "CellStore worksheet loader found an invalid shared string index")) {
                throw FastXlsxError(
                    "CellStore worksheet loader found an invalid shared string index");
            }
            if (index >= shared_strings.size()) {
                throw FastXlsxError(
                    "CellStore worksheet loader found a shared string index out of range");
            }
            value = CellValue::text(shared_strings[static_cast<std::size_t>(index)]);
            break;
        }
    }

    if (cell.style_id.has_value()) {
        value = value.with_style(*cell.style_id);
    }
    return value;
}

void reject_unsupported_value_shape(const ActiveSourceCell& cell, std::string_view element_name)
{
    if (element_name == "f") {
        // Formula text owns the cell value; supported cached-result shapes are ignored.
        if (cell.type == SourceCellType::InlineString
            || cell.type == SourceCellType::SharedString) {
            throw FastXlsxError(
                "CellStore worksheet loader found a formula in an unsupported cell type");
        }
        return;
    }

    if (element_name == "t") {
        if (cell.type != SourceCellType::InlineString) {
            throw FastXlsxError(
                "CellStore worksheet loader found inline text in a non-inline string cell");
        }
        return;
    }

    if (cell.type == SourceCellType::InlineString) {
        throw FastXlsxError(
            "CellStore worksheet loader found a non-inline value in an inline string cell");
    }
}

class CellStoreWorksheetLoader {
public:
    explicit CellStoreWorksheetLoader(
        CellStoreOptions options, const SharedStringsProvider* shared_strings_provider = nullptr)
        : store_(std::move(options))
        , shared_strings_provider_(shared_strings_provider)
    {
    }

    void consume(const WorksheetEvent& event)
    {
        switch (event.kind) {
        case WorksheetEventKind::SheetDataStart:
            if (inside_sheet_data_) {
                throw FastXlsxError("CellStore worksheet loader found nested sheetData");
            }
            if (worksheet_metadata_depth_ != 0) {
                throw FastXlsxError(
                    "CellStore worksheet loader found sheetData inside worksheet metadata");
            }
            inside_sheet_data_ = true;
            break;
        case WorksheetEventKind::SheetDataEnd:
            if (!inside_sheet_data_ || inside_row_ || active_cell_.has_value()) {
                throw FastXlsxError(
                    "CellStore worksheet loader found an invalid sheetData boundary");
            }
            inside_sheet_data_ = false;
            break;
        case WorksheetEventKind::RowStart:
            if (inside_row_) {
                throw FastXlsxError("CellStore worksheet loader found nested rows");
            }
            if (!inside_sheet_data_) {
                throw FastXlsxError("CellStore worksheet loader found a row outside sheetData");
            }
            inside_row_ = true;
            if (raw_tag_has_unsupported_attributes(event.raw_xml,
                    {"r", "spans", "s", "customFormat", "ht", "customHeight", "hidden",
                        "outlineLevel", "collapsed", "thickTop", "thickBot", "ph",
                        "x14ac:dyDescent"})) {
                throw FastXlsxError(
                    "CellStore worksheet loader does not load row metadata attributes");
            }
            if (const std::optional<std::string_view> row_attribute =
                    unqualified_attribute_value(event.raw_xml, "r")) {
                const std::uint32_t row_number = parse_row_number(
                    *row_attribute, "CellStore worksheet loader row");
                if (!seen_rows_.insert(row_number).second) {
                    throw FastXlsxError(
                        "CellStore worksheet loader found duplicate row numbers");
                }
                if (last_explicit_row_.has_value() && row_number < *last_explicit_row_) {
                    throw FastXlsxError(
                        "CellStore worksheet loader found out-of-order row numbers");
                }
                last_explicit_row_ = row_number;
            }
            break;
        case WorksheetEventKind::RowEnd:
            if (!inside_row_) {
                throw FastXlsxError("CellStore worksheet loader found a row end without a row");
            }
            inside_row_ = false;
            break;
        case WorksheetEventKind::CellStart:
            begin_cell(event);
            break;
        case WorksheetEventKind::CellValueMarkup:
            consume_value_markup(event);
            break;
        case WorksheetEventKind::CellValue:
            consume_value_text(event);
            break;
        case WorksheetEventKind::RawText:
            consume_raw_text(event);
            break;
        case WorksheetEventKind::CellEnd:
            end_cell();
            break;
        case WorksheetEventKind::Metadata:
            if (active_cell_.has_value()) {
                consume_cell_metadata(event);
            } else if (!inside_sheet_data_) {
                consume_worksheet_metadata(event);
            }
            break;
        case WorksheetEventKind::ProcessingInstruction:
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::Unsupported:
            if (active_cell_.has_value()) {
                throw FastXlsxError(
                    "CellStore worksheet loader does not load cell comments, processing instructions, or unsupported markup");
            }
            break;
        default:
            break;
        }
    }

    [[nodiscard]] CellStore finish()
    {
        if (active_cell_.has_value()) {
            throw FastXlsxError("CellStore worksheet loader ended inside an open cell");
        }
        if (inside_row_) {
            throw FastXlsxError("CellStore worksheet loader ended inside an open row");
        }
        if (inside_sheet_data_) {
            throw FastXlsxError("CellStore worksheet loader ended inside open sheetData");
        }
        if (worksheet_metadata_depth_ != 0) {
            throw FastXlsxError(
                "CellStore worksheet loader ended inside open worksheet metadata");
        }
        return std::move(store_);
    }

private:
    void begin_cell(const WorksheetEvent& event)
    {
        if (active_cell_.has_value()) {
            throw FastXlsxError("CellStore worksheet loader found nested cells");
        }
        if (!inside_row_) {
            throw FastXlsxError("CellStore worksheet loader found a cell outside a row");
        }
        const std::optional<StyleId> style_id =
            parse_source_style_attribute_for_load(event.raw_xml);
        if (raw_tag_has_unsupported_attributes(
                event.raw_xml, {"r", "t", "s", "ph"})) {
            throw FastXlsxError(
                "CellStore worksheet loader does not load cell metadata attributes");
        }

        const std::optional<std::string_view> cell_reference =
            unqualified_attribute_value(event.raw_xml, "r");
        const ParsedCellReference parsed_reference =
            parse_cell_reference_for_load(cell_reference.value_or(event.cell_reference));
        if (!event.row_number.empty()) {
            const std::uint32_t row_number = parse_row_number(
                event.row_number, "CellStore worksheet loader row");
            if (row_number != parsed_reference.row) {
                throw FastXlsxError(
                    "CellStore worksheet loader row and cell reference do not match");
            }
        }

        ActiveSourceCell cell;
        cell.position = CellPosition {parsed_reference.row, parsed_reference.column};
        if (last_source_cell_.has_value() && !(*last_source_cell_ < cell.position)) {
            throw FastXlsxError(
                "CellStore worksheet loader found out-of-order cell references");
        }
        last_source_cell_ = cell.position;
        cell.type = source_cell_type_from_raw_tag(
            event.raw_xml, shared_strings_provider_ != nullptr);
        cell.style_id = style_id;
        active_cell_ = std::move(cell);
    }

    void consume_value_markup(const WorksheetEvent& event)
    {
        if (!active_cell_.has_value()) {
            throw FastXlsxError("CellStore worksheet loader found cell value outside a cell");
        }

        ActiveSourceCell& cell = *active_cell_;
        if (cell.type == SourceCellType::InlineString
            && !cell.inline_ignored_metadata_stack.empty()) {
            consume_inline_ignored_value_markup(cell, event);
            return;
        }
        if (cell.type == SourceCellType::InlineString
            && cell.inline_rich_properties_depth > 0) {
            throw FastXlsxError(
                "CellStore worksheet loader found malformed inline rich text metadata");
        }
        if (is_closing_raw_tag(event.raw_xml)) {
            cell.current_value_element.clear();
            return;
        }

        reject_unsupported_value_shape(cell, event.element_name);
        if (event.element_name == "f" && raw_tag_has_attributes(event.raw_xml)) {
            if (raw_tag_has_unsupported_attributes(event.raw_xml, {"t", "ref", "si"})) {
                throw FastXlsxError(
                    "CellStore worksheet loader does not load unsupported formula attributes");
            }
            if (const std::optional<std::string_view> formula_type =
                    unqualified_attribute_value(event.raw_xml, "t");
                formula_type.has_value() && *formula_type == "shared") {
                cell.formula_type_is_shared = true;
            }
            if (const std::optional<std::string_view> shared_index =
                    unqualified_attribute_value(event.raw_xml, "si")) {
                std::uint64_t index = 0;
                if (!parse_shared_string_index(
                        *shared_index, index,
                        "CellStore worksheet loader found an invalid shared formula index")) {
                    throw FastXlsxError(
                        "CellStore worksheet loader found an invalid shared formula index");
                }
                cell.shared_formula_index = index;
            }
            cell.saw_formula_metadata_attributes = true;
        }
        if (event.element_name == "v" && raw_tag_has_attributes(event.raw_xml)) {
            throw FastXlsxError(
                "CellStore worksheet loader does not load scalar value attributes");
        }
        if (event.element_name == "t"
            && raw_tag_has_unsupported_attributes(event.raw_xml, "xml:space")) {
            throw FastXlsxError(
                "CellStore worksheet loader does not load inline text attributes");
        }
        remember_value_element(cell, event.element_name);
        if (!event.self_closing) {
            cell.current_value_element = std::string(event.element_name);
        }
    }

    void consume_cell_metadata(const WorksheetEvent& event)
    {
        ActiveSourceCell& cell = *active_cell_;
        if (event.element_name == "is") {
            if (cell.type != SourceCellType::InlineString) {
                throw FastXlsxError(
                    "CellStore worksheet loader found inline-string metadata in a non-inline string cell");
            }
            return;
        }

        if (cell.type == SourceCellType::InlineString) {
            consume_inline_string_metadata(cell, event);
            return;
        }

        throw FastXlsxError(
            "CellStore worksheet loader does not load unsupported cell metadata");
    }

    void consume_worksheet_metadata(const WorksheetEvent& event)
    {
        if (is_closing_raw_tag(event.raw_xml)) {
            if (worksheet_metadata_depth_ == 0) {
                throw FastXlsxError(
                    "CellStore worksheet loader found a worksheet metadata end without metadata");
            }
            --worksheet_metadata_depth_;
            return;
        }
        if (!event.self_closing) {
            ++worksheet_metadata_depth_;
        }
    }

    static void consume_inline_string_metadata(
        ActiveSourceCell& cell, const WorksheetEvent& event)
    {
        const bool closing = is_closing_raw_tag(event.raw_xml);
        if (!cell.inline_ignored_metadata_stack.empty()) {
            if (cell.inline_ignored_metadata_text_depth > 0) {
                throw FastXlsxError(
                    "CellStore worksheet loader found malformed inline rich text metadata");
            }
            if (event.element_name == "si") {
                throw FastXlsxError(
                    "CellStore worksheet loader found malformed inline rich text metadata");
            }
            if (closing) {
                if (cell.inline_ignored_metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "CellStore worksheet loader found malformed inline rich text metadata");
                }
                cell.inline_ignored_metadata_stack.pop_back();
                cell.inline_ignored_metadata_depth = cell.inline_ignored_metadata_stack.size();
            } else if (!event.self_closing) {
                cell.inline_ignored_metadata_stack.emplace_back(event.element_name);
                cell.inline_ignored_metadata_depth = cell.inline_ignored_metadata_stack.size();
            }
            return;
        }

        if (cell.inline_rich_properties_depth > 0) {
            if (closing) {
                if (event.element_name == "r") {
                    throw FastXlsxError(
                        "CellStore worksheet loader found malformed inline rich text metadata");
                }
                --cell.inline_rich_properties_depth;
            } else if (!event.self_closing) {
                ++cell.inline_rich_properties_depth;
            }
            return;
        }

        if (event.element_name == "rPh" || event.element_name == "phoneticPr"
            || event.element_name == "extLst") {
            if (closing) {
                throw FastXlsxError(
                    "CellStore worksheet loader found malformed inline rich text metadata");
            }
            if (!event.self_closing) {
                cell.inline_ignored_metadata_stack.emplace_back(event.element_name);
                cell.inline_ignored_metadata_depth = cell.inline_ignored_metadata_stack.size();
            }
            return;
        }

        if (event.element_name == "rPr") {
            if (closing || cell.inline_rich_run_depth == 0) {
                throw FastXlsxError(
                    "CellStore worksheet loader found malformed inline rich text metadata");
            }
            if (!event.self_closing) {
                ++cell.inline_rich_properties_depth;
            }
            return;
        }

        if (event.element_name == "r") {
            if (closing) {
                if (cell.inline_rich_run_depth == 0) {
                    throw FastXlsxError(
                        "CellStore worksheet loader found malformed inline rich text metadata");
                }
                --cell.inline_rich_run_depth;
            } else if (!event.self_closing) {
                ++cell.inline_rich_run_depth;
            }
            return;
        }

        throw FastXlsxError(
            "CellStore worksheet loader does not load unsupported inline string metadata");
    }

    static void consume_inline_ignored_value_markup(
        ActiveSourceCell& cell, const WorksheetEvent& event)
    {
        const bool closing = is_closing_raw_tag(event.raw_xml);
        if (event.element_name != "t") {
            if (cell.inline_ignored_metadata_text_depth > 0) {
                throw FastXlsxError(
                    "CellStore worksheet loader found malformed inline rich text metadata");
            }
            return;
        }
        if (closing) {
            if (cell.inline_ignored_metadata_text_depth == 0) {
                throw FastXlsxError(
                    "CellStore worksheet loader found malformed inline rich text metadata");
            }
            --cell.inline_ignored_metadata_text_depth;
            return;
        }
        if (cell.inline_ignored_metadata_text_depth > 0) {
            throw FastXlsxError(
                "CellStore worksheet loader found malformed inline rich text metadata");
        }
        if (!event.self_closing) {
            ++cell.inline_ignored_metadata_text_depth;
        }
    }

    static void remember_value_element(ActiveSourceCell& cell, std::string_view element_name)
    {
        if (element_name == "f") {
            if (cell.saw_formula_element) {
                throw FastXlsxError(
                    "CellStore worksheet loader found duplicate formula elements");
            }
            cell.saw_formula_element = true;
        } else if (element_name == "v") {
            if (cell.saw_scalar_value_element) {
                throw FastXlsxError(
                    "CellStore worksheet loader found duplicate scalar value elements");
            }
            cell.saw_scalar_value_element = true;
        } else if (element_name == "t") {
            const bool inside_rich_run = cell.inline_rich_run_depth > 0;
            if (!inside_rich_run && cell.saw_inline_text_element) {
                throw FastXlsxError(
                    "CellStore worksheet loader found duplicate inline text elements");
            }
            if (inside_rich_run && cell.saw_direct_inline_text_element) {
                throw FastXlsxError(
                    "CellStore worksheet loader found duplicate inline text elements");
            }
            cell.saw_inline_text_element = true;
            if (inside_rich_run) {
                return;
            } else {
                cell.saw_direct_inline_text_element = true;
            }
        }
    }

    void consume_value_text(const WorksheetEvent& event)
    {
        if (active_cell_.has_value()
            && active_cell_->type == SourceCellType::InlineString
            && active_cell_->inline_ignored_metadata_depth > 0) {
            return;
        }
        if (!active_cell_.has_value() || active_cell_->current_value_element.empty()) {
            throw FastXlsxError("CellStore worksheet loader found value text without a value tag");
        }

        ActiveSourceCell& cell = *active_cell_;
        const std::string decoded_text = unescape_xml_text(event.text);
        if (cell.current_value_element == "f") {
            cell.formula_text += decoded_text;
        } else if (cell.current_value_element == "v") {
            cell.scalar_text += decoded_text;
        } else if (cell.current_value_element == "t") {
            cell.inline_text += decoded_text;
        }
    }

    void consume_raw_text(const WorksheetEvent& event) const
    {
        if (!has_non_whitespace(event.raw_xml)) {
            return;
        }
        if (active_cell_.has_value()) {
            if (active_cell_->type == SourceCellType::InlineString
                && active_cell_->inline_ignored_metadata_depth > 0) {
                return;
            }
            throw FastXlsxError("CellStore worksheet loader found value text without a value tag");
        }
        if (inside_row_) {
            throw FastXlsxError("CellStore worksheet loader found row text outside a cell");
        }
        if (inside_sheet_data_) {
            throw FastXlsxError("CellStore worksheet loader found sheetData text outside a row");
        }
        if (worksheet_metadata_depth_ == 0) {
            throw FastXlsxError(
                "CellStore worksheet loader found worksheet text outside metadata or sheetData");
        }
    }

    void end_cell()
    {
        if (!active_cell_.has_value()) {
            throw FastXlsxError("CellStore worksheet loader found a cell end without a cell");
        }

        const ActiveSourceCell cell = std::move(*active_cell_);
        active_cell_.reset();
        if (cell.inline_rich_run_depth != 0 || cell.inline_rich_properties_depth != 0
            || cell.inline_ignored_metadata_depth != 0
            || cell.inline_ignored_metadata_text_depth != 0
            || !cell.inline_ignored_metadata_stack.empty()) {
            throw FastXlsxError(
                "CellStore worksheet loader found malformed inline rich text metadata");
        }
        if (store_.try_cell(cell.position.row, cell.position.column) != nullptr) {
            throw FastXlsxError(
                "CellStore worksheet loader found duplicate cell references");
        }
        store_.set_cell(
            cell.position.row, cell.position.column,
            materialize_cell_value(
                cell, shared_strings_provider_, shared_formula_definitions_));
        if (cell.formula_type_is_shared && cell.shared_formula_index.has_value()
            && !cell.formula_text.empty()) {
            shared_formula_definitions_[*cell.shared_formula_index] =
                SharedFormulaDefinition {cell.position, cell.formula_text};
        }
    }

    CellStore store_;
    const SharedStringsProvider* shared_strings_provider_ = nullptr;
    std::set<std::uint32_t> seen_rows_;
    std::optional<std::uint32_t> last_explicit_row_;
    std::optional<CellPosition> last_source_cell_;
    std::size_t worksheet_metadata_depth_ = 0;
    bool inside_sheet_data_ = false;
    bool inside_row_ = false;
    std::optional<ActiveSourceCell> active_cell_;
    SharedFormulaDefinitions shared_formula_definitions_;
};

} // namespace

CellRecord CellRecord::from_value(const CellValue& value)
{
    CellRecord record;
    record.kind = value.kind();
    record.number_value = value.number_value();
    record.boolean_value = value.boolean_value();
    record.text_value = value.text_value();
    if (value.has_style() && value.style_id().value() != 0) {
        record.style_id = value.style_id();
    }
    return record;
}

CellValue CellRecord::to_value() const
{
    CellValue value = CellValue::blank();
    switch (kind) {
    case CellValueKind::Blank:
        value = CellValue::blank();
        break;
    case CellValueKind::Number:
        value = CellValue::number(number_value);
        break;
    case CellValueKind::Text:
        value = CellValue::text(text_value);
        break;
    case CellValueKind::Boolean:
        value = CellValue::boolean(boolean_value);
        break;
    case CellValueKind::Formula:
        value = CellValue::formula(text_value);
        break;
    }

    if (style_id.has_value()) {
        value = value.with_style(*style_id);
    }
    return value;
}

WorksheetInputChunkCallback cell_store_sheet_data_chunk_source(const CellStore& store)
{
    auto state = std::make_shared<CellStoreSheetDataChunkSourceState>(store);
    return [state = std::move(state)](std::string& output_chunk) mutable {
        return state->read(output_chunk);
    };
}

WorksheetInputChunkCallback cell_store_worksheet_chunk_source(const CellStore& store)
{
    auto state = std::make_shared<CellStoreWorksheetChunkSourceState>(store);
    return [state = std::move(state)](std::string& output_chunk) mutable {
        return state->read(output_chunk);
    };
}

std::string cell_store_dimension_reference(const CellStore& store)
{
    if (store.empty()) {
        return "A1";
    }

    std::uint32_t first_row = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t first_column = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t last_row = 0;
    std::uint32_t last_column = 0;

    for (const auto& [position, record] : store.records()) {
        (void)record;
        first_row = std::min(first_row, position.row);
        first_column = std::min(first_column, position.column);
        last_row = std::max(last_row, position.row);
        last_column = std::max(last_column, position.column);
    }

    return range_reference(first_row, first_column, last_row, last_column);
}

CellStore load_cell_store_from_worksheet_chunks_with_shared_strings(
    const WorksheetInputChunkCallback& read_next_chunk,
    CellStoreOptions options,
    WorksheetEventReaderOptions reader_options,
    const std::vector<std::string>* shared_strings)
{
    std::optional<SharedStringsProvider> shared_strings_provider;
    if (shared_strings != nullptr) {
        shared_strings_provider = [shared_strings]() -> const std::vector<std::string>& {
            return *shared_strings;
        };
    }
    CellStoreWorksheetLoader loader(
        std::move(options),
        shared_strings_provider.has_value() ? &*shared_strings_provider : nullptr);
    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&loader](const WorksheetEvent& event) {
            loader.consume(event);
        },
        reader_options);
    return loader.finish();
}

CellStore load_cell_store_from_worksheet_chunks_with_shared_strings_provider(
    const WorksheetInputChunkCallback& read_next_chunk,
    CellStoreOptions options,
    WorksheetEventReaderOptions reader_options,
    const SharedStringsProvider* shared_strings_provider)
{
    CellStoreWorksheetLoader loader(std::move(options), shared_strings_provider);
    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&loader](const WorksheetEvent& event) {
            loader.consume(event);
        },
        reader_options);
    return loader.finish();
}

CellStore load_cell_store_from_worksheet_chunks(
    const WorksheetInputChunkCallback& read_next_chunk,
    CellStoreOptions options,
    WorksheetEventReaderOptions reader_options)
{
    return load_cell_store_from_worksheet_chunks_with_shared_strings(
        read_next_chunk, std::move(options), reader_options, nullptr);
}

CellStore load_cell_store_from_worksheet_xml(
    std::string_view worksheet_xml,
    CellStoreOptions options,
    WorksheetEventReaderOptions reader_options)
{
    std::size_t offset = 0;
    const WorksheetInputChunkCallback read_next_chunk =
        [worksheet_xml, offset](std::string& output_chunk) mutable {
            if (offset >= worksheet_xml.size()) {
                output_chunk.clear();
                return false;
            }
            output_chunk.assign(worksheet_xml.data() + offset, worksheet_xml.size() - offset);
            offset = worksheet_xml.size();
            return true;
        };
    return load_cell_store_from_worksheet_chunks(
        read_next_chunk, std::move(options), reader_options);
}

CellStore load_cell_store_from_workbook_sheet(
    const PackageReader& reader, std::string_view sheet_name, CellStoreOptions options,
    WorksheetEventReaderOptions reader_options)
{
    const PartName worksheet_part = [&reader, sheet_name] {
        try {
            return reader.worksheet_part_by_sheet_name(sheet_name);
        } catch (const std::exception& error) {
            throw FastXlsxError("failed to resolve workbook sheet '" + std::string(sheet_name)
                + "' for CellStore loading: " + error.what());
        }
    }();
    const std::string worksheet_zip_entry = worksheet_part.zip_path();
    try {
        std::optional<std::vector<std::string>> shared_strings;
        SharedStringsProvider shared_strings_provider = [&reader, &shared_strings]()
            -> const std::vector<std::string>& {
            if (!shared_strings.has_value()) {
                std::optional<std::vector<std::string>> loaded_shared_strings =
                    load_shared_strings_from_workbook(reader);
                if (!loaded_shared_strings.has_value()) {
                    throw FastXlsxError(
                        "CellStore worksheet loader found shared string indexes without a sharedStrings table");
                }
                shared_strings = std::move(*loaded_shared_strings);
            }
            return *shared_strings;
        };
        return load_cell_store_from_worksheet_chunks_with_shared_strings_provider(
            reader.entry_chunk_source(worksheet_zip_entry),
            std::move(options), reader_options,
            &shared_strings_provider);
    } catch (const std::exception& error) {
        throw FastXlsxError("failed to load CellStore from workbook sheet '"
            + std::string(sheet_name) + "' worksheet part '" + worksheet_part.value()
            + "' ZIP entry '" + worksheet_zip_entry + "': " + error.what());
    }
}

CellStore::CellStore(CellStoreOptions options)
    : options_(std::move(options))
{
}

void CellStore::set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value)
{
    validate_position(row, column);
    const CellPosition position {row, column};
    CellRecord record = CellRecord::from_value(value);
    const auto existing = cells_.find(position);
    const bool inserting_new_record = existing == cells_.end();
    const std::size_t next_cell_count = cells_.size() + (inserting_new_record ? 1U : 0U);

    if (options_.max_cells.has_value() && next_cell_count > *options_.max_cells) {
        throw FastXlsxError("CellStore max_cells guardrail exceeded");
    }

    if (options_.memory_budget_bytes.has_value()) {
        std::size_t next_memory_usage = estimated_memory_usage();
        if (!inserting_new_record) {
            next_memory_usage -= entry_memory_usage(existing->first, existing->second);
        }
        next_memory_usage += entry_memory_usage(position, record);
        if (next_memory_usage > *options_.memory_budget_bytes) {
            throw FastXlsxError("CellStore memory_budget_bytes guardrail exceeded");
        }
    }

    cells_[position] = std::move(record);
}

void CellStore::erase_cell(std::uint32_t row, std::uint32_t column)
{
    validate_position(row, column);
    cells_.erase(CellPosition {row, column});
}

const CellRecord* CellStore::try_cell(std::uint32_t row, std::uint32_t column) const
{
    validate_position(row, column);
    const auto iterator = cells_.find(CellPosition {row, column});
    if (iterator == cells_.end()) {
        return nullptr;
    }
    return &iterator->second;
}

const CellRecord* CellStore::find_cell(std::uint32_t row, std::uint32_t column) const
{
    return try_cell(row, column);
}

bool CellStore::empty() const noexcept
{
    return cells_.empty();
}

std::size_t CellStore::cell_count() const noexcept
{
    return cells_.size();
}

std::size_t CellStore::estimated_memory_usage() const noexcept
{
    std::size_t total = sizeof(CellStore);
    for (const auto& [position, record] : cells_) {
        total += entry_memory_usage(position, record);
    }
    return total;
}

const CellStoreOptions& CellStore::options() const noexcept
{
    return options_;
}

const std::map<CellPosition, CellRecord>& CellStore::records() const noexcept
{
    return cells_;
}

} // namespace fastxlsx::detail
