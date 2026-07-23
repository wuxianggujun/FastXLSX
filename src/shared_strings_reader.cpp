#include "shared_strings_reader.hpp"

#include "bounded_xml_reader.hpp"

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
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

void validate_qualified_name(std::string_view name)
{
    const std::size_t colon = name.find(':');
    if (colon == 0 || colon + 1 == name.size()
        || (colon != std::string_view::npos
            && name.find(':', colon + 1) != std::string_view::npos)) {
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found an invalid qualified XML element name");
    }
}

struct ParsedTag {
    std::string_view qualified_name;
    std::string_view name;
    bool closing = false;
    bool self_closing = false;
};

std::string_view tag_body(std::string_view raw_tag)
{
    if (raw_tag.size() < 3 || raw_tag.front() != '<' || raw_tag.back() != '>') {
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found invalid XML markup");
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
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found an empty XML tag");
    }

    std::size_t name_end = 0;
    while (name_end < body.size() && !is_space(body[name_end])
        && body[name_end] != '/' && body[name_end] != '?') {
        ++name_end;
    }
    if (name_end == 0) {
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found an empty XML element name");
    }
    result.qualified_name = body.substr(0, name_end);
    validate_qualified_name(result.qualified_name);
    result.name = local_name(result.qualified_name);
    if (result.closing && has_non_whitespace(body.substr(name_end))) {
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found unexpected closing-tag content");
    }
    return result;
}

template <typename Visitor>
void visit_attributes(std::string_view raw_tag, Visitor&& visitor)
{
    std::string_view body = tag_body(raw_tag);
    if (!body.empty() && body.front() == '/') {
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader does not allow attributes on closing tags");
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
                "sharedStrings reader found an invalid attribute name");
        }
        const std::string_view name = body.substr(name_begin, position - name_begin);

        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] != '=') {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader found an attribute without a value");
        }
        ++position;
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size()
            || (body[position] != '"' && body[position] != '\'')) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader found an unquoted attribute value");
        }

        const char quote = body[position++];
        const std::size_t value_begin = position;
        while (position < body.size() && body[position] != quote) {
            ++position;
        }
        if (position >= body.size()) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader found an unterminated attribute value");
        }
        visitor(name, body.substr(value_begin, position - value_begin));
        ++position;
        if (position < body.size() && !is_space(body[position])) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader requires whitespace between attributes");
        }
    }
}

void require_decimal_attribute(std::string_view value, std::string_view name)
{
    if (value.empty()
        || !std::all_of(value.begin(), value.end(), [](char ch) {
               return ch >= '0' && ch <= '9';
           })) {
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found an invalid " + std::string(name) + " attribute");
    }
}

void validate_root_attributes(std::string_view raw_tag)
{
    bool saw_count = false;
    bool saw_unique_count = false;
    visit_attributes(raw_tag, [&](std::string_view name, std::string_view value) {
        if (name == "count") {
            if (saw_count) {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader found a duplicate count attribute");
            }
            saw_count = true;
            require_decimal_attribute(value, "count");
            return;
        }
        if (name == "uniqueCount") {
            if (saw_unique_count) {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader found a duplicate uniqueCount attribute");
            }
            saw_unique_count = true;
            require_decimal_attribute(value, "uniqueCount");
            return;
        }
        if (name == "xmlns" || name.starts_with("xmlns:")) {
            return;
        }
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found unsupported root metadata");
    });
}

void validate_item_attributes(std::string_view raw_tag)
{
    visit_attributes(raw_tag, [](std::string_view, std::string_view) {
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found unsupported item metadata");
    });
}

void validate_text_attributes(std::string_view raw_tag)
{
    bool saw_space = false;
    visit_attributes(raw_tag, [&](std::string_view name, std::string_view value) {
        if (name != "xml:space" || saw_space
            || (value != "preserve" && value != "default")) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader found unsupported text metadata");
        }
        saw_space = true;
    });
}

void require_no_attributes(std::string_view raw_tag, std::string_view context)
{
    visit_attributes(raw_tag, [context](std::string_view, std::string_view) {
        throw fastxlsx::FastXlsxError(
            "shared string runs reader found unsupported " + std::string(context)
            + " metadata");
    });
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
            "shared string runs reader found an invalid " + std::string(context));
    }
    return static_cast<std::uint32_t>(parsed);
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
        "shared string runs reader found an invalid " + std::string(context));
}

std::uint32_t parse_argb(std::string_view value, std::string_view context)
{
    if (value.size() != 8) {
        throw fastxlsx::FastXlsxError(
            "shared string runs reader found an invalid " + std::string(context));
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
                "shared string runs reader found an invalid " + std::string(context));
        }
        parsed = (parsed << 4U) | digit;
    }
    return parsed;
}

std::string_view parse_required_value(
    std::string_view raw_tag, std::string_view context)
{
    std::optional<std::string_view> parsed;
    visit_attributes(raw_tag, [&](std::string_view name, std::string_view value) {
        if (name != "val" || parsed.has_value()) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found unsupported "
                + std::string(context) + " metadata");
        }
        parsed = value;
    });
    if (!parsed.has_value()) {
        throw fastxlsx::FastXlsxError(
            "shared string runs reader requires a " + std::string(context)
            + " val attribute");
    }
    return *parsed;
}

bool parse_flag(std::string_view raw_tag, std::string_view context)
{
    std::optional<bool> parsed;
    visit_attributes(raw_tag, [&](std::string_view name, std::string_view value) {
        if (name != "val" || parsed.has_value()) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found unsupported "
                + std::string(context) + " metadata");
        }
        parsed = parse_boolean(value, context);
    });
    return parsed.value_or(true);
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
            "sharedStrings reader found an invalid XML character reference");
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
            "sharedStrings reader found an unknown XML entity reference");
    }

    std::size_t offset = 1;
    std::uint32_t base = 10;
    if (offset < entity.size() && (entity[offset] == 'x' || entity[offset] == 'X')) {
        base = 16;
        ++offset;
    }
    if (offset == entity.size()) {
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found an invalid XML character reference");
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
                "sharedStrings reader found an invalid XML character reference");
        }
        value = value * base + digit;
    }
    return value;
}

void append_decoded_text(std::string& output,
    std::string_view text,
    std::size_t max_text_bytes,
    std::string_view reader_name,
    std::string_view limit_name)
{
    for (std::size_t offset = 0; offset < text.size(); ++offset) {
        if (text[offset] != '&') {
            output.push_back(text[offset]);
        } else {
            const std::size_t semicolon = text.find(';', offset + 1);
            if (semicolon == std::string_view::npos) {
                throw fastxlsx::FastXlsxError(
                    std::string(reader_name)
                    + " found an unterminated XML entity reference");
            }
            const std::string_view entity =
                text.substr(offset + 1, semicolon - offset - 1);
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
                append_utf8(output, parse_character_reference(entity));
            }
            offset = semicolon;
        }
        if (output.size() > max_text_bytes) {
            throw fastxlsx::FastXlsxError(
                std::string(reader_name) + " exceeded " + std::string(limit_name));
        }
    }
}

class SharedStringProjectionReader {
public:
    SharedStringProjectionReader(const fastxlsx::SharedStringReadCallbacks& callbacks,
        fastxlsx::SharedStringReaderOptions options)
        : callbacks_(callbacks)
        , options_(options)
    {
    }

    void consume_text(std::string_view text)
    {
        if (!inside_text_) {
            std::size_t offset = 0;
            if (!saw_root_ && text.substr(0, 3) == "\xef\xbb\xbf") {
                offset = 3;
            }
            if (has_non_whitespace(text.substr(offset))) {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader found text outside a text element");
            }
            return;
        }

        append_decoded_text(item_text_, text, options_.max_item_text_bytes,
            "sharedStrings reader", "max_item_text_bytes");
        summary_.peak_item_text_bytes =
            std::max(summary_.peak_item_text_bytes, item_text_.size());
    }

    void consume_special_markup()
    {
        if (inside_item_ || inside_text_) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader found unsupported markup inside an item");
        }
    }

    void consume_tag(std::string_view raw_tag)
    {
        const ParsedTag tag = parse_tag(raw_tag);
        if (tag.closing) {
            consume_closing_tag(tag.name);
            return;
        }
        consume_opening_tag(tag, raw_tag);
    }

    [[nodiscard]] fastxlsx::SharedStringReadSummary finish() const
    {
        if (!saw_root_) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader requires an sst root element");
        }
        if (!finished_root_ || inside_root_ || inside_item_ || inside_text_) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader ended inside an open XML element");
        }
        return summary_;
    }

private:
    void consume_opening_tag(const ParsedTag& tag, std::string_view raw_tag)
    {
        if (!saw_root_) {
            if (tag.name != "sst") {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader requires an sst root element");
            }
            validate_root_attributes(raw_tag);
            saw_root_ = true;
            inside_root_ = !tag.self_closing;
            finished_root_ = tag.self_closing;
            return;
        }
        if (finished_root_) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader found markup after the sst root");
        }
        if (inside_text_) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader found nested markup inside simple text");
        }
        if (inside_item_) {
            if (tag.name == "r") {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader does not project rich text runs");
            }
            if (tag.name == "rPh" || tag.name == "phoneticPr") {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader does not project phonetic metadata");
            }
            if (tag.name == "extLst" || tag.name == "ext") {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader does not project extension metadata");
            }
            if (tag.name != "t" || saw_text_) {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader requires exactly one simple text element per item");
            }
            validate_text_attributes(raw_tag);
            saw_text_ = true;
            inside_text_ = !tag.self_closing;
            return;
        }
        if (!inside_root_) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader found invalid root state");
        }
        if (tag.name == "extLst" || tag.name == "ext") {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader does not project extension metadata");
        }
        if (tag.name != "si" || tag.self_closing) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader requires nonempty si item elements");
        }
        validate_item_attributes(raw_tag);
        inside_item_ = true;
        saw_text_ = false;
        item_text_.clear();
    }

    void consume_closing_tag(std::string_view name)
    {
        if (inside_text_) {
            if (name != "t") {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader found a mismatched text boundary");
            }
            inside_text_ = false;
            return;
        }
        if (inside_item_) {
            if (name != "si" || !saw_text_) {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader requires exactly one simple text element per item");
            }
            emit_item();
            inside_item_ = false;
            saw_text_ = false;
            item_text_.clear();
            return;
        }
        if (inside_root_ && name == "sst") {
            inside_root_ = false;
            finished_root_ = true;
            return;
        }
        throw fastxlsx::FastXlsxError(
            "sharedStrings reader found a mismatched XML boundary");
    }

    void emit_item()
    {
        if (summary_.item_count
            > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader item index exceeds uint32_t");
        }
        if (callbacks_.on_item) {
            const fastxlsx::SharedStringItemView item {
                static_cast<std::uint32_t>(summary_.item_count), item_text_};
            callbacks_.on_item(item);
        }
        ++summary_.item_count;
    }

    const fastxlsx::SharedStringReadCallbacks& callbacks_;
    fastxlsx::SharedStringReaderOptions options_;
    bool saw_root_ = false;
    bool inside_root_ = false;
    bool finished_root_ = false;
    bool inside_item_ = false;
    bool saw_text_ = false;
    bool inside_text_ = false;
    std::string item_text_;
    fastxlsx::SharedStringReadSummary summary_;
};

enum class SharedStringItemShape {
    Unknown,
    Simple,
    Rich,
};

struct ActiveSharedStringRun {
    fastxlsx::SharedStringRunKind kind = fastxlsx::SharedStringRunKind::SimpleText;
    fastxlsx::SharedStringRunFormat format;
    bool saw_properties = false;
    bool saw_bold = false;
    bool saw_italic = false;
    bool saw_font = false;
    bool saw_size = false;
    bool saw_family = false;
    bool saw_scheme = false;
    bool saw_color = false;
    bool saw_text = false;
};

class SharedStringRunProjectionReader {
public:
    SharedStringRunProjectionReader(
        const fastxlsx::SharedStringRunReadCallbacks& callbacks,
        fastxlsx::SharedStringRunReaderOptions options)
        : callbacks_(callbacks)
        , options_(options)
    {
    }

    void consume_text(std::string_view text)
    {
        if (!inside_text_) {
            std::size_t offset = 0;
            if (!saw_root_ && text.substr(0, 3) == "\xef\xbb\xbf") {
                offset = 3;
            }
            if (has_non_whitespace(text.substr(offset))) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found text outside a text element");
            }
            return;
        }

        const std::size_t previous_size = run_text_.size();
        append_decoded_text(run_text_, text, options_.max_run_text_bytes,
            "shared string runs reader", "max_run_text_bytes");
        const std::size_t added = run_text_.size() - previous_size;
        if (added > options_.max_item_text_bytes - item_text_bytes_) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader exceeded max_item_text_bytes");
        }
        item_text_bytes_ += added;
        summary_.peak_run_text_bytes =
            std::max(summary_.peak_run_text_bytes, run_text_.size());
        summary_.peak_item_text_bytes =
            std::max(summary_.peak_item_text_bytes, item_text_bytes_);
    }

    void consume_special_markup() const
    {
        if (inside_item_) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found unsupported markup inside an item");
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

    [[nodiscard]] fastxlsx::SharedStringRunReadSummary finish() const
    {
        if (!saw_root_) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader requires an sst root element");
        }
        if (!finished_root_ || !element_stack_.empty() || inside_item_
            || inside_text_ || inside_run_properties_ || active_run_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader ended inside an open XML element");
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
                "shared string runs reader exceeded max_xml_nesting_depth");
        }
        element_stack_.emplace_back(qualified_name);
        summary_.peak_xml_nesting_depth =
            std::max(summary_.peak_xml_nesting_depth, element_stack_.size());
    }

    void begin_item(std::string_view raw_tag)
    {
        if (tag_is_self_closing(raw_tag)) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader requires nonempty si item elements");
        }
        if (summary_.item_count
            > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader item index exceeds uint32_t");
        }
        validate_item_attributes(raw_tag);
        inside_item_ = true;
        item_shape_ = SharedStringItemShape::Unknown;
        current_item_index_ = static_cast<std::uint32_t>(summary_.item_count);
        current_item_run_count_ = 0;
        item_text_bytes_ = 0;
        active_run_.reset();
        run_text_.clear();
        push_element(parse_tag(raw_tag).qualified_name);
        if (callbacks_.on_item_start) {
            callbacks_.on_item_start({current_item_index_});
        }
    }

    static bool tag_is_self_closing(std::string_view raw_tag)
    {
        return parse_tag(raw_tag).self_closing;
    }

    void begin_run(fastxlsx::SharedStringRunKind kind)
    {
        if (current_item_run_count_ >= options_.max_runs_per_item) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader exceeded max_runs_per_item");
        }
        active_run_.emplace();
        active_run_->kind = kind;
        run_text_.clear();
    }

    void consume_run_property(const ParsedTag& tag, std::string_view raw_tag)
    {
        if (parent_local_name() != "rPr" || !active_run_.has_value()
            || active_run_->kind != fastxlsx::SharedStringRunKind::RichText) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found nested run properties");
        }

        bool* seen = nullptr;
        if (tag.name == "b") {
            seen = &active_run_->saw_bold;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found duplicate bold metadata");
            }
            *seen = true;
            active_run_->format.bold = parse_flag(raw_tag, "run bold flag");
        } else if (tag.name == "i") {
            seen = &active_run_->saw_italic;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found duplicate italic metadata");
            }
            *seen = true;
            active_run_->format.italic = parse_flag(raw_tag, "run italic flag");
        } else if (tag.name == "rFont") {
            seen = &active_run_->saw_font;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found duplicate run font metadata");
            }
            *seen = true;
            if (parse_required_value(raw_tag, "run font") != "Calibri") {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader does not project non-default run font");
            }
        } else if (tag.name == "sz") {
            seen = &active_run_->saw_size;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found duplicate run size metadata");
            }
            *seen = true;
            const std::string_view value = parse_required_value(raw_tag, "run size");
            if (value != "11" && value != "11.0") {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader does not project non-default run size");
            }
        } else if (tag.name == "family") {
            seen = &active_run_->saw_family;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found duplicate run family metadata");
            }
            *seen = true;
            if (parse_u32(parse_required_value(raw_tag, "run family"), "run family")
                != 2) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader does not project non-default run family");
            }
        } else if (tag.name == "scheme") {
            seen = &active_run_->saw_scheme;
            if (*seen) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found duplicate run scheme metadata");
            }
            *seen = true;
            if (parse_required_value(raw_tag, "run scheme") != "minor") {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader does not project non-default run scheme");
            }
        } else if (tag.name == "color") {
            if (active_run_->saw_color) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found duplicate run color metadata");
            }
            active_run_->saw_color = true;
            std::optional<std::uint32_t> rgb;
            std::optional<std::uint32_t> theme;
            visit_attributes(raw_tag,
                [&](std::string_view name, std::string_view value) {
                    if (name == "rgb") {
                        if (rgb.has_value()) {
                            throw fastxlsx::FastXlsxError(
                                "shared string runs reader found duplicate run color metadata");
                        }
                        rgb = parse_argb(value, "run ARGB color");
                        return;
                    }
                    if (name == "theme") {
                        if (theme.has_value()) {
                            throw fastxlsx::FastXlsxError(
                                "shared string runs reader found duplicate run color metadata");
                        }
                        theme = parse_u32(value, "run theme color");
                        return;
                    }
                    throw fastxlsx::FastXlsxError(
                        "shared string runs reader found unsupported run color metadata");
                });
            if (rgb.has_value() == theme.has_value()) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader requires one run rgb or theme color");
            }
            if (theme.has_value() && *theme != 1) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader does not resolve non-default run theme color");
            }
            active_run_->format.direct_argb_color = rgb;
        } else {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found unsupported run property");
        }

        if (!tag.self_closing) {
            push_element(tag.qualified_name);
        }
    }

    void consume_rich_child(const ParsedTag& tag, std::string_view raw_tag)
    {
        if (!active_run_.has_value()
            || active_run_->kind != fastxlsx::SharedStringRunKind::RichText
            || parent_local_name() != "r") {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found nested rich run metadata");
        }
        if (tag.name == "rPr") {
            if (active_run_->saw_properties || active_run_->saw_text) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found misplaced run properties");
            }
            require_no_attributes(raw_tag, "rPr");
            active_run_->saw_properties = true;
            if (!tag.self_closing) {
                inside_run_properties_ = true;
                push_element(tag.qualified_name);
            }
            return;
        }
        if (tag.name == "t") {
            if (active_run_->saw_text) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader requires exactly one text element per run");
            }
            validate_text_attributes(raw_tag);
            active_run_->saw_text = true;
            if (!tag.self_closing) {
                inside_text_ = true;
                push_element(tag.qualified_name);
            }
            return;
        }
        throw fastxlsx::FastXlsxError(
            "shared string runs reader found unsupported rich run child");
    }

    void consume_opening_tag(const ParsedTag& tag, std::string_view raw_tag)
    {
        if (!saw_root_) {
            if (tag.name != "sst") {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader requires an sst root element");
            }
            validate_root_attributes(raw_tag);
            saw_root_ = true;
            if (tag.self_closing) {
                finished_root_ = true;
            } else {
                inside_root_ = true;
                push_element(tag.qualified_name);
            }
            return;
        }
        if (finished_root_) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found markup after the sst root");
        }
        if (inside_text_) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found nested markup inside text");
        }
        if (inside_run_properties_) {
            consume_run_property(tag, raw_tag);
            return;
        }
        if (inside_item_ && (tag.name == "rPh" || tag.name == "phoneticPr")) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader does not project phonetic metadata");
        }
        if (inside_item_ && (tag.name == "extLst" || tag.name == "ext")) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader does not project extension metadata");
        }
        if (active_run_.has_value()) {
            if (active_run_->kind == fastxlsx::SharedStringRunKind::RichText) {
                consume_rich_child(tag, raw_tag);
                return;
            }
            if (tag.name == "r") {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found mixed simple and rich item content");
            }
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found markup after simple text");
        }
        if (!inside_item_) {
            if (!inside_root_) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found invalid root state");
            }
            if (tag.name == "extLst" || tag.name == "ext") {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader does not project extension metadata");
            }
            if (tag.name != "si" || tag.self_closing) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader requires nonempty si item elements");
            }
            begin_item(raw_tag);
            return;
        }

        if (item_shape_ == SharedStringItemShape::Unknown) {
            if (tag.name == "t") {
                validate_text_attributes(raw_tag);
                item_shape_ = SharedStringItemShape::Simple;
                begin_run(fastxlsx::SharedStringRunKind::SimpleText);
                active_run_->saw_text = true;
                if (!tag.self_closing) {
                    inside_text_ = true;
                    push_element(tag.qualified_name);
                }
                return;
            }
            if (tag.name == "r") {
                if (tag.self_closing) {
                    throw fastxlsx::FastXlsxError(
                        "shared string runs reader requires nonempty rich runs");
                }
                require_no_attributes(raw_tag, "rich run");
                item_shape_ = SharedStringItemShape::Rich;
                begin_run(fastxlsx::SharedStringRunKind::RichText);
                push_element(tag.qualified_name);
                return;
            }
        } else if (item_shape_ == SharedStringItemShape::Simple) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found mixed or duplicate simple item content");
        } else {
            if (tag.name == "t") {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found mixed rich and simple item content");
            }
            if (tag.name == "r") {
                if (tag.self_closing) {
                    throw fastxlsx::FastXlsxError(
                        "shared string runs reader requires nonempty rich runs");
                }
                require_no_attributes(raw_tag, "rich run");
                begin_run(fastxlsx::SharedStringRunKind::RichText);
                push_element(tag.qualified_name);
                return;
            }
        }

        if (tag.name == "rPh" || tag.name == "phoneticPr") {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader does not project phonetic metadata");
        }
        if (tag.name == "extLst" || tag.name == "ext") {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader does not project extension metadata");
        }
        throw fastxlsx::FastXlsxError(
            "shared string runs reader found unsupported item metadata");
    }

    void consume_closing_tag(const ParsedTag& tag)
    {
        if (element_stack_.empty() || element_stack_.back() != tag.qualified_name) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader found a mismatched XML boundary");
        }

        const std::string_view name = local_name(element_stack_.back());
        if (inside_text_) {
            if (name != "t") {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader found a mismatched text boundary");
            }
            inside_text_ = false;
            element_stack_.pop_back();
            return;
        }
        if (inside_run_properties_) {
            const bool closes_properties = name == "rPr";
            element_stack_.pop_back();
            if (closes_properties) {
                inside_run_properties_ = false;
            }
            return;
        }
        if (active_run_.has_value()
            && active_run_->kind == fastxlsx::SharedStringRunKind::RichText
            && name == "r") {
            if (!active_run_->saw_text) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader requires exactly one text element per run");
            }
            element_stack_.pop_back();
            emit_run();
            return;
        }
        if (inside_item_ && name == "si") {
            finish_item();
            element_stack_.pop_back();
            return;
        }
        if (inside_root_ && name == "sst") {
            inside_root_ = false;
            finished_root_ = true;
            element_stack_.pop_back();
            return;
        }
        throw fastxlsx::FastXlsxError(
            "shared string runs reader found a mismatched XML boundary");
    }

    void emit_run()
    {
        if (!active_run_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader ended without an active run");
        }
        if (current_item_run_count_
            > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader run index exceeds uint32_t");
        }
        const fastxlsx::SharedStringRunView view {
            current_item_index_,
            static_cast<std::uint32_t>(current_item_run_count_),
            active_run_->kind,
            run_text_,
            active_run_->format};
        if (callbacks_.on_run) {
            callbacks_.on_run(view);
        }
        ++current_item_run_count_;
        ++summary_.run_count;
        summary_.peak_runs_per_item =
            std::max(summary_.peak_runs_per_item, current_item_run_count_);
        active_run_.reset();
        run_text_.clear();
    }

    void finish_item()
    {
        if (item_shape_ == SharedStringItemShape::Simple) {
            if (!active_run_.has_value() || !active_run_->saw_text) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader requires exactly one text element per item");
            }
            emit_run();
        } else if (item_shape_ == SharedStringItemShape::Rich) {
            if (active_run_.has_value() || current_item_run_count_ == 0) {
                throw fastxlsx::FastXlsxError(
                    "shared string runs reader requires one or more complete rich runs");
            }
        } else {
            throw fastxlsx::FastXlsxError(
                "shared string runs reader requires text or rich runs per item");
        }
        if (callbacks_.on_item_end) {
            callbacks_.on_item_end({current_item_index_});
        }
        ++summary_.item_count;
        inside_item_ = false;
        item_shape_ = SharedStringItemShape::Unknown;
        current_item_run_count_ = 0;
        item_text_bytes_ = 0;
        run_text_.clear();
    }

    const fastxlsx::SharedStringRunReadCallbacks& callbacks_;
    fastxlsx::SharedStringRunReaderOptions options_;
    bool saw_root_ = false;
    bool inside_root_ = false;
    bool finished_root_ = false;
    bool inside_item_ = false;
    bool inside_text_ = false;
    bool inside_run_properties_ = false;
    std::uint32_t current_item_index_ = 0;
    std::size_t current_item_run_count_ = 0;
    std::size_t item_text_bytes_ = 0;
    SharedStringItemShape item_shape_ = SharedStringItemShape::Unknown;
    std::optional<ActiveSharedStringRun> active_run_;
    std::string run_text_;
    std::vector<std::string> element_stack_;
    fastxlsx::SharedStringRunReadSummary summary_;
};

} // namespace

namespace fastxlsx::detail {

SharedStringReadSummary read_shared_strings_from_chunk_source(
    const SharedStringsInputChunkCallback& read_next_chunk,
    const SharedStringReadCallbacks& callbacks,
    SharedStringReaderOptions options)
{
    if (!read_next_chunk) {
        throw FastXlsxError("sharedStrings reader requires a chunk source");
    }
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError(
            "sharedStrings reader requires nonzero max_xml_window_bytes");
    }
    if (options.max_item_text_bytes == 0) {
        throw FastXlsxError(
            "sharedStrings reader requires nonzero max_item_text_bytes");
    }

    SharedStringProjectionReader reader(callbacks, options);
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
        options.max_xml_window_bytes, "sharedStrings");
    return reader.finish();
}

SharedStringRunReadSummary read_shared_string_runs_from_chunk_source(
    const SharedStringsInputChunkCallback& read_next_chunk,
    const SharedStringRunReadCallbacks& callbacks,
    SharedStringRunReaderOptions options)
{
    if (!read_next_chunk) {
        throw FastXlsxError("shared string runs reader requires a chunk source");
    }
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError(
            "shared string runs reader requires nonzero max_xml_window_bytes");
    }
    if (options.max_item_text_bytes == 0) {
        throw FastXlsxError(
            "shared string runs reader requires nonzero max_item_text_bytes");
    }
    if (options.max_run_text_bytes == 0) {
        throw FastXlsxError(
            "shared string runs reader requires nonzero max_run_text_bytes");
    }
    if (options.max_runs_per_item == 0) {
        throw FastXlsxError(
            "shared string runs reader requires nonzero max_runs_per_item");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError(
            "shared string runs reader requires nonzero max_xml_nesting_depth");
    }

    SharedStringRunProjectionReader reader(callbacks, options);
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
        options.max_xml_window_bytes, "shared string runs");
    return reader.finish();
}

} // namespace fastxlsx::detail
