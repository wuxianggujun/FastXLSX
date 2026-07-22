#include "styles_reader.hpp"

#include "bounded_xml_reader.hpp"

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

bool is_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

bool has_non_whitespace(std::string_view text) noexcept
{
    return std::any_of(text.begin(), text.end(), [](char ch) { return !is_space(ch); });
}

std::string_view trim(std::string_view value) noexcept
{
    while (!value.empty() && is_space(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && is_space(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

std::string_view local_name(std::string_view name) noexcept
{
    const std::size_t colon = name.rfind(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

struct ParsedTag {
    std::string_view qualified_name;
    std::string_view local_name;
    bool closing = false;
    bool self_closing = false;
};

std::string_view tag_body(std::string_view raw_tag)
{
    if (raw_tag.size() < 3 || raw_tag.front() != '<' || raw_tag.back() != '>') {
        throw fastxlsx::FastXlsxError("styles reader found invalid XML markup");
    }
    return trim(raw_tag.substr(1, raw_tag.size() - 2));
}

ParsedTag parse_tag(std::string_view raw_tag)
{
    std::string_view body = tag_body(raw_tag);
    ParsedTag result;
    if (!body.empty() && body.front() == '/') {
        result.closing = true;
        body.remove_prefix(1);
        body = trim(body);
    }
    if (!result.closing && !body.empty() && body.back() == '/') {
        result.self_closing = true;
        body.remove_suffix(1);
        body = trim(body);
    }
    if (body.empty()) {
        throw fastxlsx::FastXlsxError("styles reader found an empty XML tag");
    }

    std::size_t name_end = 0;
    while (name_end < body.size() && !is_space(body[name_end])
        && body[name_end] != '/' && body[name_end] != '?') {
        ++name_end;
    }
    if (name_end == 0) {
        throw fastxlsx::FastXlsxError("styles reader found an empty XML element name");
    }
    result.qualified_name = body.substr(0, name_end);
    result.local_name = local_name(result.qualified_name);
    if (result.closing && has_non_whitespace(body.substr(name_end))) {
        throw fastxlsx::FastXlsxError(
            "styles reader found unexpected closing-tag content");
    }
    return result;
}

template <typename Visitor>
void visit_attributes(std::string_view raw_tag, Visitor&& visitor)
{
    std::string_view body = tag_body(raw_tag);
    if (!body.empty() && body.front() == '/') {
        throw fastxlsx::FastXlsxError(
            "styles reader does not allow attributes on closing tags");
    }
    if (!body.empty() && body.back() == '/') {
        body.remove_suffix(1);
        body = trim(body);
    }

    std::size_t position = 0;
    while (position < body.size() && !is_space(body[position])) {
        ++position;
    }
    while (position < body.size()) {
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size()) {
            return;
        }

        const std::size_t name_begin = position;
        while (position < body.size() && !is_space(body[position])
            && body[position] != '=' && body[position] != '/') {
            ++position;
        }
        if (position == name_begin) {
            throw fastxlsx::FastXlsxError(
                "styles reader found an invalid attribute name");
        }
        const std::string_view name = body.substr(name_begin, position - name_begin);

        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] != '=') {
            throw fastxlsx::FastXlsxError(
                "styles reader found an attribute without a value");
        }
        ++position;
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size()
            || (body[position] != '"' && body[position] != '\'')) {
            throw fastxlsx::FastXlsxError(
                "styles reader found an unquoted attribute value");
        }

        const char quote = body[position++];
        const std::size_t value_begin = position;
        while (position < body.size() && body[position] != quote) {
            ++position;
        }
        if (position >= body.size()) {
            throw fastxlsx::FastXlsxError(
                "styles reader found an unterminated attribute value");
        }
        visitor(name, body.substr(value_begin, position - value_begin));
        ++position;
        if (position < body.size() && !is_space(body[position])) {
            throw fastxlsx::FastXlsxError(
                "styles reader requires whitespace between attributes");
        }
    }
}

std::uint32_t parse_u32(std::string_view value, std::string_view context)
{
    std::uint64_t parsed = 0;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (value.empty() || result.ec != std::errc {} || result.ptr != end
        || parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw fastxlsx::FastXlsxError(
            "styles reader found an invalid " + std::string(context));
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint64_t parse_u64(std::string_view value, std::string_view context)
{
    std::uint64_t parsed = 0;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (value.empty() || result.ec != std::errc {} || result.ptr != end) {
        throw fastxlsx::FastXlsxError(
            "styles reader found an invalid " + std::string(context));
    }
    return parsed;
}

bool parse_boolean(std::string_view value, std::string_view context)
{
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    throw fastxlsx::FastXlsxError(
        "styles reader found an invalid " + std::string(context));
}

std::uint32_t parse_argb(std::string_view value, std::string_view context)
{
    if (value.size() != 8) {
        throw fastxlsx::FastXlsxError(
            "style components reader found an invalid " + std::string(context));
    }

    std::uint32_t parsed = 0;
    for (const char ch : value) {
        std::uint32_t digit = 0;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<std::uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = static_cast<std::uint32_t>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            digit = static_cast<std::uint32_t>(ch - 'A' + 10);
        } else {
            throw fastxlsx::FastXlsxError(
                "style components reader found an invalid " + std::string(context));
        }
        parsed = (parsed << 4U) | digit;
    }
    return parsed;
}

bool is_valid_xml_code_point(std::uint32_t code_point) noexcept
{
    return code_point == 0x09U || code_point == 0x0AU || code_point == 0x0DU
        || (code_point >= 0x20U && code_point <= 0xD7FFU)
        || (code_point >= 0xE000U && code_point <= 0xFFFDU)
        || (code_point >= 0x10000U && code_point <= 0x10FFFFU);
}

void append_utf8(std::string& output, std::uint32_t code_point)
{
    if (!is_valid_xml_code_point(code_point)) {
        throw fastxlsx::FastXlsxError(
            "styles reader found an invalid XML character reference");
    }
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

std::uint32_t parse_character_reference(std::string_view entity)
{
    if (entity.size() < 2 || entity.front() != '#') {
        throw fastxlsx::FastXlsxError(
            "styles reader found an unknown XML entity reference");
    }

    std::size_t offset = 1;
    std::uint32_t base = 10;
    if (offset < entity.size() && (entity[offset] == 'x' || entity[offset] == 'X')) {
        base = 16;
        ++offset;
    }
    if (offset == entity.size()) {
        throw fastxlsx::FastXlsxError(
            "styles reader found an invalid XML character reference");
    }

    std::uint32_t value = 0;
    for (; offset < entity.size(); ++offset) {
        const char ch = entity[offset];
        std::uint32_t digit = base;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<std::uint32_t>(ch - '0');
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = static_cast<std::uint32_t>(ch - 'a' + 10);
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = static_cast<std::uint32_t>(ch - 'A' + 10);
        }
        if (digit >= base || value > (0x10FFFFU - digit) / base) {
            throw fastxlsx::FastXlsxError(
                "styles reader found an invalid XML character reference");
        }
        value = value * base + digit;
    }
    return value;
}

std::string decode_attribute_value(std::string_view value, std::size_t max_bytes)
{
    std::string decoded;
    decoded.reserve(std::min(value.size(), max_bytes));
    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        if (value[offset] != '&') {
            decoded.push_back(value[offset]);
        } else {
            const std::size_t semicolon = value.find(';', offset + 1);
            if (semicolon == std::string_view::npos) {
                throw fastxlsx::FastXlsxError(
                    "styles reader found an unterminated XML entity reference");
            }
            const std::string_view entity =
                value.substr(offset + 1, semicolon - offset - 1);
            if (entity == "amp") {
                decoded.push_back('&');
            } else if (entity == "lt") {
                decoded.push_back('<');
            } else if (entity == "gt") {
                decoded.push_back('>');
            } else if (entity == "quot") {
                decoded.push_back('"');
            } else if (entity == "apos") {
                decoded.push_back('\'');
            } else {
                append_utf8(decoded, parse_character_reference(entity));
            }
            offset = semicolon;
        }
        if (decoded.size() > max_bytes) {
            throw fastxlsx::FastXlsxError(
                "styles reader exceeded max_format_code_bytes");
        }
    }
    return decoded;
}

template <typename T>
void assign_once(std::optional<T>& destination,
    T value,
    std::string_view attribute_name)
{
    if (destination.has_value()) {
        throw fastxlsx::FastXlsxError(
            "styles reader found a duplicate " + std::string(attribute_name)
            + " attribute");
    }
    destination = std::move(value);
}

std::optional<std::uint64_t> parse_container_count(
    std::string_view raw_tag, std::string_view container_name)
{
    std::optional<std::uint64_t> count;
    visit_attributes(raw_tag, [&](std::string_view name, std::string_view value) {
        if (name != "count") {
            throw fastxlsx::FastXlsxError(
                "styles reader found unsupported " + std::string(container_name)
                + " metadata");
        }
        assign_once(count, parse_u64(value, std::string(container_name) + " count"),
            "count");
    });
    return count;
}

struct ActiveNumberFormat {
    std::optional<std::uint32_t> id;
    std::optional<std::string> format_code;
};

ActiveNumberFormat parse_number_format(std::string_view raw_tag, std::size_t max_bytes)
{
    ActiveNumberFormat format;
    visit_attributes(raw_tag, [&](std::string_view name, std::string_view value) {
        if (name == "numFmtId") {
            assign_once(format.id, parse_u32(value, "numFmtId"), name);
            return;
        }
        if (name == "formatCode") {
            assign_once(format.format_code, decode_attribute_value(value, max_bytes), name);
            return;
        }
        throw fastxlsx::FastXlsxError(
            "styles reader found unsupported number format metadata");
    });
    if (!format.id.has_value() || !format.format_code.has_value()) {
        throw fastxlsx::FastXlsxError(
            "styles reader requires numFmtId and formatCode attributes");
    }
    return format;
}

struct ActiveCellFormat {
    fastxlsx::CellFormatView view;
    std::optional<std::uint32_t> border_id;
    std::optional<std::uint32_t> xf_id;
    bool saw_number_format_id = false;
    bool saw_font_id = false;
    bool saw_fill_id = false;
    bool saw_apply_border = false;
    bool saw_apply_protection = false;
    bool saw_quote_prefix = false;
    bool saw_pivot_button = false;
};

ActiveCellFormat parse_cell_format(std::string_view raw_tag)
{
    ActiveCellFormat format;
    visit_attributes(raw_tag, [&](std::string_view name, std::string_view value) {
        if (name == "numFmtId") {
            if (format.saw_number_format_id) {
                throw fastxlsx::FastXlsxError(
                    "styles reader found a duplicate numFmtId attribute");
            }
            format.saw_number_format_id = true;
            format.view.number_format_id = parse_u32(value, "numFmtId");
            return;
        }
        if (name == "fontId") {
            if (format.saw_font_id) {
                throw fastxlsx::FastXlsxError(
                    "styles reader found a duplicate fontId attribute");
            }
            format.saw_font_id = true;
            format.view.font_id = parse_u32(value, "fontId");
            return;
        }
        if (name == "fillId") {
            if (format.saw_fill_id) {
                throw fastxlsx::FastXlsxError(
                    "styles reader found a duplicate fillId attribute");
            }
            format.saw_fill_id = true;
            format.view.fill_id = parse_u32(value, "fillId");
            return;
        }
        if (name == "borderId") {
            assign_once(format.border_id, parse_u32(value, "borderId"), name);
            return;
        }
        if (name == "xfId") {
            assign_once(format.xf_id, parse_u32(value, "xfId"), name);
            return;
        }
        if (name == "applyNumberFormat") {
            assign_once(format.view.apply_number_format,
                parse_boolean(value, "applyNumberFormat"), name);
            return;
        }
        if (name == "applyFont") {
            assign_once(
                format.view.apply_font, parse_boolean(value, "applyFont"), name);
            return;
        }
        if (name == "applyFill") {
            assign_once(
                format.view.apply_fill, parse_boolean(value, "applyFill"), name);
            return;
        }
        if (name == "applyAlignment") {
            assign_once(format.view.apply_alignment,
                parse_boolean(value, "applyAlignment"), name);
            return;
        }
        if (name == "applyBorder" || name == "applyProtection"
            || name == "quotePrefix" || name == "pivotButton") {
            bool* saw_attribute = nullptr;
            if (name == "applyBorder") {
                saw_attribute = &format.saw_apply_border;
            } else if (name == "applyProtection") {
                saw_attribute = &format.saw_apply_protection;
            } else if (name == "quotePrefix") {
                saw_attribute = &format.saw_quote_prefix;
            } else {
                saw_attribute = &format.saw_pivot_button;
            }
            if (*saw_attribute) {
                throw fastxlsx::FastXlsxError(
                    "styles reader found a duplicate " + std::string(name)
                    + " attribute");
            }
            *saw_attribute = true;
            if (parse_boolean(value, name)) {
                throw fastxlsx::FastXlsxError(
                    "styles reader does not project enabled " + std::string(name));
            }
            return;
        }
        throw fastxlsx::FastXlsxError(
            "styles reader found unsupported cell format metadata");
    });
    if (format.border_id.value_or(0) != 0) {
        throw fastxlsx::FastXlsxError(
            "styles reader does not project non-default borders");
    }
    if (format.xf_id.value_or(0) != 0) {
        throw fastxlsx::FastXlsxError(
            "styles reader does not project base style references");
    }
    return format;
}

fastxlsx::CellFormatAlignmentView parse_alignment(std::string_view raw_tag)
{
    fastxlsx::CellFormatAlignmentView alignment;
    visit_attributes(raw_tag, [&](std::string_view name, std::string_view value) {
        if (name == "wrapText") {
            assign_once(alignment.wrap_text, parse_boolean(value, "wrapText"), name);
            return;
        }
        if (name == "horizontal") {
            fastxlsx::CellFormatHorizontalAlignment parsed;
            if (value == "left") {
                parsed = fastxlsx::CellFormatHorizontalAlignment::Left;
            } else if (value == "center") {
                parsed = fastxlsx::CellFormatHorizontalAlignment::Center;
            } else if (value == "right") {
                parsed = fastxlsx::CellFormatHorizontalAlignment::Right;
            } else {
                throw fastxlsx::FastXlsxError(
                    "styles reader found unsupported horizontal alignment");
            }
            assign_once(alignment.horizontal, parsed, name);
            return;
        }
        if (name == "vertical") {
            fastxlsx::CellFormatVerticalAlignment parsed;
            if (value == "top") {
                parsed = fastxlsx::CellFormatVerticalAlignment::Top;
            } else if (value == "center") {
                parsed = fastxlsx::CellFormatVerticalAlignment::Center;
            } else if (value == "bottom") {
                parsed = fastxlsx::CellFormatVerticalAlignment::Bottom;
            } else {
                throw fastxlsx::FastXlsxError(
                    "styles reader found unsupported vertical alignment");
            }
            assign_once(alignment.vertical, parsed, name);
            return;
        }
        throw fastxlsx::FastXlsxError(
            "styles reader found unsupported alignment metadata");
    });
    return alignment;
}

class CellFormatProjectionReader {
public:
    CellFormatProjectionReader(const fastxlsx::CellFormatReadCallbacks& callbacks,
        fastxlsx::CellFormatReaderOptions options)
        : callbacks_(callbacks)
        , options_(options)
    {
    }

    void consume_text(std::string_view text) const
    {
        std::size_t offset = 0;
        if (!saw_root_ && text.substr(0, 3) == "\xef\xbb\xbf") {
            offset = 3;
        }
        if (has_non_whitespace(text.substr(offset))) {
            throw fastxlsx::FastXlsxError(
                "styles reader found unsupported element text");
        }
    }

    void consume_special_markup() const
    {
        if (active_number_format_.has_value() || active_cell_format_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "styles reader found unsupported markup inside a projected record");
        }
    }

    void consume_tag(std::string_view raw_tag)
    {
        const ParsedTag tag = parse_tag(raw_tag);
        if (tag.closing) {
            consume_closing_tag(tag);
            return;
        }
        consume_opening_tag(tag, raw_tag);
    }

    [[nodiscard]] fastxlsx::CellFormatReadSummary finish() const
    {
        if (!saw_root_) {
            throw fastxlsx::FastXlsxError(
                "styles reader requires a styleSheet root element");
        }
        if (!finished_root_ || !element_stack_.empty()
            || active_number_format_.has_value() || active_cell_format_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "styles reader ended inside an open XML element");
        }
        if (!saw_cell_xfs_ || summary_.cell_format_count == 0) {
            throw fastxlsx::FastXlsxError(
                "styles reader requires at least one cellXfs record");
        }
        return summary_;
    }

private:
    std::string_view parent_local_name() const noexcept
    {
        return element_stack_.empty()
            ? std::string_view {}
            : local_name(element_stack_.back());
    }

    void push_element(std::string_view qualified_name)
    {
        if (element_stack_.size() >= options_.max_xml_nesting_depth) {
            throw fastxlsx::FastXlsxError(
                "styles reader exceeded max_xml_nesting_depth");
        }
        element_stack_.emplace_back(qualified_name);
        summary_.peak_xml_nesting_depth =
            std::max(summary_.peak_xml_nesting_depth, element_stack_.size());
    }

    void consume_opening_tag(const ParsedTag& tag, std::string_view raw_tag)
    {
        if (!saw_root_) {
            if (tag.local_name != "styleSheet") {
                throw fastxlsx::FastXlsxError(
                    "styles reader requires a styleSheet root element");
            }
            visit_attributes(raw_tag, [](std::string_view, std::string_view) {});
            saw_root_ = true;
            if (tag.self_closing) {
                finished_root_ = true;
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }
        if (finished_root_) {
            throw fastxlsx::FastXlsxError(
                "styles reader found markup after the root element");
        }

        const std::string_view parent = parent_local_name();
        if (active_number_format_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "styles reader found nested number format metadata");
        }
        if (active_cell_format_.has_value()) {
            if (parent != "xf" || tag.local_name != "alignment"
                || active_cell_format_->view.alignment.has_value()) {
                throw fastxlsx::FastXlsxError(
                    "styles reader found unsupported nested cell format metadata");
            }
            active_cell_format_->view.alignment = parse_alignment(raw_tag);
            if (!tag.self_closing) {
                push_element(tag.qualified_name);
            }
            return;
        }

        if (parent == "styleSheet" && tag.local_name == "numFmts") {
            if (saw_num_fmts_) {
                throw fastxlsx::FastXlsxError(
                    "styles reader found duplicate numFmts elements");
            }
            if (saw_cell_xfs_) {
                throw fastxlsx::FastXlsxError(
                    "styles reader requires numFmts before cellXfs");
            }
            saw_num_fmts_ = true;
            declared_num_format_count_ = parse_container_count(raw_tag, "numFmts");
            if (tag.self_closing) {
                validate_count(declared_num_format_count_,
                    summary_.custom_number_format_count, "numFmts");
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }
        if (parent == "styleSheet" && tag.local_name == "cellXfs") {
            if (saw_cell_xfs_) {
                throw fastxlsx::FastXlsxError(
                    "styles reader found duplicate cellXfs elements");
            }
            saw_cell_xfs_ = true;
            declared_cell_format_count_ = parse_container_count(raw_tag, "cellXfs");
            if (tag.self_closing) {
                validate_count(declared_cell_format_count_,
                    summary_.cell_format_count, "cellXfs");
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }
        if (parent == "numFmts") {
            if (tag.local_name != "numFmt") {
                throw fastxlsx::FastXlsxError(
                    "styles reader found unsupported numFmts child metadata");
            }
            active_number_format_ =
                parse_number_format(raw_tag, options_.max_format_code_bytes);
            if (tag.self_closing) {
                emit_number_format();
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }
        if (parent == "cellXfs") {
            if (tag.local_name != "xf") {
                throw fastxlsx::FastXlsxError(
                    "styles reader found unsupported cellXfs child metadata");
            }
            active_cell_format_ = parse_cell_format(raw_tag);
            if (tag.self_closing) {
                emit_cell_format();
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }

        visit_attributes(raw_tag, [](std::string_view, std::string_view) {});
        if (!tag.self_closing) {
            push_element(tag.qualified_name);
        }
    }

    void consume_closing_tag(const ParsedTag& tag)
    {
        if (element_stack_.empty() || element_stack_.back() != tag.qualified_name) {
            throw fastxlsx::FastXlsxError(
                "styles reader found a mismatched XML boundary");
        }

        const std::string_view name = local_name(element_stack_.back());
        if (name == "alignment") {
            element_stack_.pop_back();
            return;
        }
        if (name == "numFmt" && active_number_format_.has_value()) {
            element_stack_.pop_back();
            emit_number_format();
            return;
        }
        if (name == "xf" && active_cell_format_.has_value()) {
            element_stack_.pop_back();
            emit_cell_format();
            return;
        }
        if (name == "numFmts") {
            validate_count(declared_num_format_count_,
                summary_.custom_number_format_count, "numFmts");
        } else if (name == "cellXfs") {
            validate_count(declared_cell_format_count_,
                summary_.cell_format_count, "cellXfs");
        } else if (name == "styleSheet") {
            finished_root_ = true;
        }
        element_stack_.pop_back();
    }

    static void validate_count(const std::optional<std::uint64_t>& declared,
        std::uint64_t actual,
        std::string_view container_name)
    {
        if (declared.has_value() && *declared != actual) {
            throw fastxlsx::FastXlsxError(
                "styles reader found a " + std::string(container_name)
                + " count mismatch");
        }
    }

    void emit_number_format()
    {
        if (summary_.custom_number_format_count
            >= options_.max_custom_number_format_count) {
            throw fastxlsx::FastXlsxError(
                "styles reader exceeded max_custom_number_format_count");
        }
        if (!seen_number_format_ids_.insert(*active_number_format_->id).second) {
            throw fastxlsx::FastXlsxError(
                "styles reader found a duplicate custom numFmtId");
        }
        const std::size_t format_size = active_number_format_->format_code->size();
        summary_.peak_format_code_bytes =
            std::max(summary_.peak_format_code_bytes, format_size);
        if (callbacks_.on_number_format) {
            const fastxlsx::NumberFormatView view {
                *active_number_format_->id, *active_number_format_->format_code};
            callbacks_.on_number_format(view);
        }
        ++summary_.custom_number_format_count;
        active_number_format_.reset();
    }

    void emit_cell_format()
    {
        if (summary_.cell_format_count
            > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw fastxlsx::FastXlsxError(
                "styles reader cell format index exceeds uint32_t");
        }
        active_cell_format_->view.index =
            static_cast<std::uint32_t>(summary_.cell_format_count);
        if (callbacks_.on_cell_format) {
            callbacks_.on_cell_format(active_cell_format_->view);
        }
        ++summary_.cell_format_count;
        active_cell_format_.reset();
    }

    const fastxlsx::CellFormatReadCallbacks& callbacks_;
    fastxlsx::CellFormatReaderOptions options_;
    bool saw_root_ = false;
    bool finished_root_ = false;
    bool saw_num_fmts_ = false;
    bool saw_cell_xfs_ = false;
    std::optional<std::uint64_t> declared_num_format_count_;
    std::optional<std::uint64_t> declared_cell_format_count_;
    std::optional<ActiveNumberFormat> active_number_format_;
    std::optional<ActiveCellFormat> active_cell_format_;
    std::unordered_set<std::uint32_t> seen_number_format_ids_;
    std::vector<std::string> element_stack_;
    fastxlsx::CellFormatReadSummary summary_;
};

struct ActiveFontComponent {
    fastxlsx::CellFormatFontView view;
    bool saw_bold = false;
    bool saw_italic = false;
    bool saw_size = false;
    bool saw_color = false;
    bool saw_name = false;
    bool saw_family = false;
    bool saw_scheme = false;
};

struct ActiveFillComponent {
    std::optional<fastxlsx::CellFormatFillPattern> pattern;
    std::optional<std::uint32_t> foreground_argb_color;
    bool saw_background = false;
};

class StyleComponentProjectionReader {
public:
    StyleComponentProjectionReader(
        const fastxlsx::StyleComponentReadCallbacks& callbacks,
        fastxlsx::StyleComponentReaderOptions options)
        : callbacks_(callbacks)
        , options_(options)
    {
    }

    void consume_text(std::string_view text) const
    {
        std::size_t offset = 0;
        if (!saw_root_ && text.substr(0, 3) == "\xef\xbb\xbf") {
            offset = 3;
        }
        if (has_non_whitespace(text.substr(offset))) {
            throw fastxlsx::FastXlsxError(
                "style components reader found unsupported element text");
        }
    }

    void consume_special_markup() const
    {
        if (active_font_.has_value() || active_fill_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "style components reader found unsupported markup inside a component record");
        }
    }

    void consume_tag(std::string_view raw_tag)
    {
        const ParsedTag tag = parse_tag(raw_tag);
        if (tag.closing) {
            consume_closing_tag(tag);
            return;
        }
        consume_opening_tag(tag, raw_tag);
    }

    [[nodiscard]] fastxlsx::StyleComponentReadSummary finish() const
    {
        if (!saw_root_) {
            throw fastxlsx::FastXlsxError(
                "style components reader requires a styleSheet root element");
        }
        if (!finished_root_ || !element_stack_.empty()
            || active_font_.has_value() || active_fill_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "style components reader ended inside an open XML element");
        }
        return summary_;
    }

private:
    std::string_view parent_local_name() const noexcept
    {
        return element_stack_.empty()
            ? std::string_view {}
            : local_name(element_stack_.back());
    }

    void push_element(std::string_view qualified_name)
    {
        if (element_stack_.size() >= options_.max_xml_nesting_depth) {
            throw fastxlsx::FastXlsxError(
                "style components reader exceeded max_xml_nesting_depth");
        }
        element_stack_.emplace_back(qualified_name);
        summary_.peak_xml_nesting_depth =
            std::max(summary_.peak_xml_nesting_depth, element_stack_.size());
    }

    static void require_no_attributes(
        std::string_view raw_tag, std::string_view context)
    {
        visit_attributes(raw_tag, [context](std::string_view, std::string_view) {
            throw fastxlsx::FastXlsxError(
                "style components reader found unsupported " + std::string(context)
                + " metadata");
        });
    }

    static bool parse_flag(std::string_view raw_tag, std::string_view context)
    {
        std::optional<bool> value;
        visit_attributes(raw_tag, [&](std::string_view name, std::string_view raw_value) {
            if (name != "val") {
                throw fastxlsx::FastXlsxError(
                    "style components reader found unsupported "
                    + std::string(context) + " metadata");
            }
            assign_once(value, parse_boolean(raw_value, context), name);
        });
        return value.value_or(true);
    }

    static std::string_view parse_required_value(
        std::string_view raw_tag, std::string_view context)
    {
        std::optional<std::string_view> value;
        visit_attributes(raw_tag, [&](std::string_view name, std::string_view raw_value) {
            if (name != "val") {
                throw fastxlsx::FastXlsxError(
                    "style components reader found unsupported "
                    + std::string(context) + " metadata");
            }
            assign_once(value, raw_value, name);
        });
        if (!value.has_value()) {
            throw fastxlsx::FastXlsxError(
                "style components reader requires a " + std::string(context)
                + " val attribute");
        }
        return *value;
    }

    void consume_font_child(const ParsedTag& tag, std::string_view raw_tag)
    {
        if (parent_local_name() != "font") {
            throw fastxlsx::FastXlsxError(
                "style components reader found nested font metadata");
        }

        bool* seen = nullptr;
        if (tag.local_name == "b") {
            seen = &active_font_->saw_bold;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "style components reader found duplicate bold metadata");
            }
            *seen = true;
            active_font_->view.bold = parse_flag(raw_tag, "font bold flag");
        } else if (tag.local_name == "i") {
            seen = &active_font_->saw_italic;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "style components reader found duplicate italic metadata");
            }
            *seen = true;
            active_font_->view.italic = parse_flag(raw_tag, "font italic flag");
        } else if (tag.local_name == "sz") {
            seen = &active_font_->saw_size;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "style components reader found duplicate font size metadata");
            }
            *seen = true;
            const std::string_view value = parse_required_value(raw_tag, "font size");
            if (value != "11" && value != "11.0") {
                throw fastxlsx::FastXlsxError(
                    "style components reader does not project non-default font size");
            }
        } else if (tag.local_name == "color") {
            if (active_font_->saw_color) {
                throw fastxlsx::FastXlsxError(
                    "style components reader found duplicate font color metadata");
            }
            active_font_->saw_color = true;
            std::optional<std::uint32_t> rgb;
            std::optional<std::uint32_t> theme;
            visit_attributes(raw_tag,
                [&](std::string_view name, std::string_view value) {
                    if (name == "rgb") {
                        assign_once(rgb, parse_argb(value, "font ARGB color"), name);
                        return;
                    }
                    if (name == "theme") {
                        assign_once(theme, parse_u32(value, "font theme color"), name);
                        return;
                    }
                    throw fastxlsx::FastXlsxError(
                        "style components reader found unsupported font color metadata");
                });
            if (rgb.has_value() == theme.has_value()) {
                throw fastxlsx::FastXlsxError(
                    "style components reader requires one font rgb or theme color");
            }
            if (theme.has_value() && *theme != 1) {
                throw fastxlsx::FastXlsxError(
                    "style components reader does not resolve non-default font theme color");
            }
            active_font_->view.direct_argb_color = rgb;
        } else if (tag.local_name == "name") {
            seen = &active_font_->saw_name;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "style components reader found duplicate font name metadata");
            }
            *seen = true;
            if (parse_required_value(raw_tag, "font name") != "Calibri") {
                throw fastxlsx::FastXlsxError(
                    "style components reader does not project non-default font name");
            }
        } else if (tag.local_name == "family") {
            seen = &active_font_->saw_family;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "style components reader found duplicate font family metadata");
            }
            *seen = true;
            if (parse_u32(parse_required_value(raw_tag, "font family"), "font family")
                != 2) {
                throw fastxlsx::FastXlsxError(
                    "style components reader does not project non-default font family");
            }
        } else if (tag.local_name == "scheme") {
            seen = &active_font_->saw_scheme;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "style components reader found duplicate font scheme metadata");
            }
            *seen = true;
            if (parse_required_value(raw_tag, "font scheme") != "minor") {
                throw fastxlsx::FastXlsxError(
                    "style components reader does not project non-default font scheme");
            }
        } else {
            throw fastxlsx::FastXlsxError(
                "style components reader found unsupported font metadata");
        }

        if (!tag.self_closing) {
            push_element(tag.qualified_name);
        }
    }

    void consume_fill_child(const ParsedTag& tag, std::string_view raw_tag)
    {
        const std::string_view parent = parent_local_name();
        if (parent == "fill") {
            if (tag.local_name != "patternFill" || active_fill_->pattern.has_value()) {
                throw fastxlsx::FastXlsxError(
                    "style components reader requires one patternFill per fill");
            }
            std::optional<std::string_view> pattern_type;
            visit_attributes(raw_tag,
                [&](std::string_view name, std::string_view value) {
                    if (name != "patternType") {
                        throw fastxlsx::FastXlsxError(
                            "style components reader found unsupported patternFill metadata");
                    }
                    assign_once(pattern_type, value, name);
                });
            if (!pattern_type.has_value()) {
                throw fastxlsx::FastXlsxError(
                    "style components reader requires patternType metadata");
            }
            if (*pattern_type == "none") {
                active_fill_->pattern = fastxlsx::CellFormatFillPattern::None;
            } else if (*pattern_type == "gray125") {
                active_fill_->pattern = fastxlsx::CellFormatFillPattern::Gray125;
            } else if (*pattern_type == "solid") {
                active_fill_->pattern = fastxlsx::CellFormatFillPattern::Solid;
            } else {
                throw fastxlsx::FastXlsxError(
                    "style components reader found unsupported fill pattern");
            }
        } else if (parent == "patternFill") {
            if (tag.local_name == "fgColor") {
                if (active_fill_->foreground_argb_color.has_value()) {
                    throw fastxlsx::FastXlsxError(
                        "style components reader found duplicate fill foreground color");
                }
                std::optional<std::uint32_t> rgb;
                visit_attributes(raw_tag,
                    [&](std::string_view name, std::string_view value) {
                        if (name != "rgb") {
                            throw fastxlsx::FastXlsxError(
                                "style components reader requires a direct fill foreground color");
                        }
                        assign_once(rgb, parse_argb(value, "fill ARGB color"), name);
                    });
                if (!rgb.has_value()) {
                    throw fastxlsx::FastXlsxError(
                        "style components reader requires a fill foreground rgb color");
                }
                active_fill_->foreground_argb_color = rgb;
            } else if (tag.local_name == "bgColor") {
                if (active_fill_->saw_background) {
                    throw fastxlsx::FastXlsxError(
                        "style components reader found duplicate fill background color");
                }
                active_fill_->saw_background = true;
                std::optional<std::uint32_t> indexed;
                visit_attributes(raw_tag,
                    [&](std::string_view name, std::string_view value) {
                        if (name != "indexed") {
                            throw fastxlsx::FastXlsxError(
                                "style components reader requires the default fill background color");
                        }
                        assign_once(indexed,
                            parse_u32(value, "fill background color"), name);
                    });
                if (!indexed.has_value() || *indexed != 64) {
                    throw fastxlsx::FastXlsxError(
                        "style components reader requires indexed fill background 64");
                }
            } else {
                throw fastxlsx::FastXlsxError(
                    "style components reader found unsupported patternFill child metadata");
            }
        } else {
            throw fastxlsx::FastXlsxError(
                "style components reader found nested fill metadata");
        }

        if (!tag.self_closing) {
            push_element(tag.qualified_name);
        }
    }

    void consume_opening_tag(const ParsedTag& tag, std::string_view raw_tag)
    {
        if (!saw_root_) {
            if (tag.local_name != "styleSheet") {
                throw fastxlsx::FastXlsxError(
                    "style components reader requires a styleSheet root element");
            }
            visit_attributes(raw_tag, [](std::string_view, std::string_view) {});
            saw_root_ = true;
            if (tag.self_closing) {
                finished_root_ = true;
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }
        if (finished_root_) {
            throw fastxlsx::FastXlsxError(
                "style components reader found markup after the root element");
        }
        if (active_font_.has_value()) {
            consume_font_child(tag, raw_tag);
            return;
        }
        if (active_fill_.has_value()) {
            consume_fill_child(tag, raw_tag);
            return;
        }

        const std::string_view parent = parent_local_name();
        if (parent == "styleSheet" && tag.local_name == "fonts") {
            if (saw_fonts_) {
                throw fastxlsx::FastXlsxError(
                    "style components reader found duplicate fonts elements");
            }
            if (saw_fills_) {
                throw fastxlsx::FastXlsxError(
                    "style components reader requires fonts before fills");
            }
            saw_fonts_ = true;
            declared_font_count_ = parse_container_count(raw_tag, "fonts");
            if (declared_font_count_.has_value()
                && *declared_font_count_ > options_.max_font_count) {
                throw fastxlsx::FastXlsxError(
                    "style components reader exceeded max_font_count");
            }
            if (tag.self_closing) {
                validate_count(declared_font_count_, summary_.font_count, "fonts");
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }
        if (parent == "styleSheet" && tag.local_name == "fills") {
            if (saw_fills_) {
                throw fastxlsx::FastXlsxError(
                    "style components reader found duplicate fills elements");
            }
            saw_fills_ = true;
            declared_fill_count_ = parse_container_count(raw_tag, "fills");
            if (declared_fill_count_.has_value()
                && *declared_fill_count_ > options_.max_fill_count) {
                throw fastxlsx::FastXlsxError(
                    "style components reader exceeded max_fill_count");
            }
            if (tag.self_closing) {
                validate_count(declared_fill_count_, summary_.fill_count, "fills");
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }
        if (parent == "fonts") {
            if (tag.local_name != "font") {
                throw fastxlsx::FastXlsxError(
                    "style components reader found unsupported fonts child metadata");
            }
            require_no_attributes(raw_tag, "font");
            active_font_.emplace();
            if (tag.self_closing) {
                emit_font();
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }
        if (parent == "fills") {
            if (tag.local_name != "fill") {
                throw fastxlsx::FastXlsxError(
                    "style components reader found unsupported fills child metadata");
            }
            require_no_attributes(raw_tag, "fill");
            active_fill_.emplace();
            if (tag.self_closing) {
                emit_fill();
            } else {
                push_element(tag.qualified_name);
            }
            return;
        }

        visit_attributes(raw_tag, [](std::string_view, std::string_view) {});
        if (!tag.self_closing) {
            push_element(tag.qualified_name);
        }
    }

    void consume_closing_tag(const ParsedTag& tag)
    {
        if (element_stack_.empty() || element_stack_.back() != tag.qualified_name) {
            throw fastxlsx::FastXlsxError(
                "style components reader found a mismatched XML boundary");
        }

        const std::string_view name = local_name(element_stack_.back());
        if (active_font_.has_value()) {
            const bool closes_font = name == "font";
            element_stack_.pop_back();
            if (closes_font) {
                emit_font();
            }
            return;
        }
        if (active_fill_.has_value()) {
            const bool closes_fill = name == "fill";
            element_stack_.pop_back();
            if (closes_fill) {
                emit_fill();
            }
            return;
        }

        if (name == "fonts") {
            validate_count(declared_font_count_, summary_.font_count, "fonts");
        } else if (name == "fills") {
            validate_count(declared_fill_count_, summary_.fill_count, "fills");
        } else if (name == "styleSheet") {
            finished_root_ = true;
        }
        element_stack_.pop_back();
    }

    static void validate_count(const std::optional<std::uint64_t>& declared,
        std::uint64_t actual,
        std::string_view container_name)
    {
        if (declared.has_value() && *declared != actual) {
            throw fastxlsx::FastXlsxError(
                "style components reader found a " + std::string(container_name)
                + " count mismatch");
        }
    }

    void emit_font()
    {
        if (summary_.font_count >= options_.max_font_count) {
            throw fastxlsx::FastXlsxError(
                "style components reader exceeded max_font_count");
        }
        if (summary_.font_count
            > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw fastxlsx::FastXlsxError(
                "style components reader font index exceeds uint32_t");
        }
        active_font_->view.index = static_cast<std::uint32_t>(summary_.font_count);
        if (callbacks_.on_font) {
            callbacks_.on_font(active_font_->view);
        }
        ++summary_.font_count;
        active_font_.reset();
    }

    void emit_fill()
    {
        if (!active_fill_->pattern.has_value()) {
            throw fastxlsx::FastXlsxError(
                "style components reader requires one patternFill per fill");
        }
        if (*active_fill_->pattern == fastxlsx::CellFormatFillPattern::Solid) {
            if (!active_fill_->foreground_argb_color.has_value()) {
                throw fastxlsx::FastXlsxError(
                    "style components reader requires a solid fill foreground color");
            }
        } else if (active_fill_->foreground_argb_color.has_value()
            || active_fill_->saw_background) {
            throw fastxlsx::FastXlsxError(
                "style components reader found color metadata on a non-solid fill");
        }
        if (summary_.fill_count >= options_.max_fill_count) {
            throw fastxlsx::FastXlsxError(
                "style components reader exceeded max_fill_count");
        }
        if (summary_.fill_count
            > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw fastxlsx::FastXlsxError(
                "style components reader fill index exceeds uint32_t");
        }

        const fastxlsx::CellFormatFillView view {
            static_cast<std::uint32_t>(summary_.fill_count),
            *active_fill_->pattern,
            active_fill_->foreground_argb_color};
        if (callbacks_.on_fill) {
            callbacks_.on_fill(view);
        }
        ++summary_.fill_count;
        active_fill_.reset();
    }

    const fastxlsx::StyleComponentReadCallbacks& callbacks_;
    fastxlsx::StyleComponentReaderOptions options_;
    bool saw_root_ = false;
    bool finished_root_ = false;
    bool saw_fonts_ = false;
    bool saw_fills_ = false;
    std::optional<std::uint64_t> declared_font_count_;
    std::optional<std::uint64_t> declared_fill_count_;
    std::optional<ActiveFontComponent> active_font_;
    std::optional<ActiveFillComponent> active_fill_;
    std::vector<std::string> element_stack_;
    fastxlsx::StyleComponentReadSummary summary_;
};

} // namespace

namespace fastxlsx::detail {

CellFormatReadSummary read_cell_formats_from_chunk_source(
    const StylesInputChunkCallback& read_next_chunk,
    const CellFormatReadCallbacks& callbacks,
    CellFormatReaderOptions options)
{
    if (!read_next_chunk) {
        throw FastXlsxError("styles reader requires a chunk source");
    }
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError("styles reader requires nonzero max_xml_window_bytes");
    }
    if (options.max_format_code_bytes == 0) {
        throw FastXlsxError("styles reader requires nonzero max_format_code_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError("styles reader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_custom_number_format_count == 0) {
        throw FastXlsxError(
            "styles reader requires nonzero max_custom_number_format_count");
    }

    CellFormatProjectionReader reader(callbacks, options);
    BoundedXmlCallbacks xml_callbacks;
    xml_callbacks.on_text = [&reader](std::string_view text) {
        reader.consume_text(text);
    };
    xml_callbacks.on_tag = [&reader](std::string_view raw_tag) {
        reader.consume_tag(raw_tag);
    };
    xml_callbacks.on_special_markup = [&reader] {
        reader.consume_special_markup();
    };
    scan_bounded_xml_from_chunk_source(
        read_next_chunk, xml_callbacks, options.max_xml_window_bytes, "styles");
    return reader.finish();
}

StyleComponentReadSummary read_style_components_from_chunk_source(
    const StylesInputChunkCallback& read_next_chunk,
    const StyleComponentReadCallbacks& callbacks,
    StyleComponentReaderOptions options)
{
    if (!read_next_chunk) {
        throw FastXlsxError("style components reader requires a chunk source");
    }
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError(
            "style components reader requires nonzero max_xml_window_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError(
            "style components reader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_font_count == 0) {
        throw FastXlsxError("style components reader requires nonzero max_font_count");
    }
    if (options.max_fill_count == 0) {
        throw FastXlsxError("style components reader requires nonzero max_fill_count");
    }

    StyleComponentProjectionReader reader(callbacks, options);
    BoundedXmlCallbacks xml_callbacks;
    xml_callbacks.on_text = [&reader](std::string_view text) {
        reader.consume_text(text);
    };
    xml_callbacks.on_tag = [&reader](std::string_view raw_tag) {
        reader.consume_tag(raw_tag);
    };
    xml_callbacks.on_special_markup = [&reader] {
        reader.consume_special_markup();
    };
    scan_bounded_xml_from_chunk_source(read_next_chunk, xml_callbacks,
        options.max_xml_window_bytes, "style components");
    return reader.finish();
}

} // namespace fastxlsx::detail
