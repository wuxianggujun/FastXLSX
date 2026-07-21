#include "shared_strings_reader.hpp"

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

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
    result.name = local_name(body.substr(0, name_end));
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

        for (std::size_t offset = 0; offset < text.size(); ++offset) {
            if (text[offset] != '&') {
                item_text_.push_back(text[offset]);
            } else {
                const std::size_t semicolon = text.find(';', offset + 1);
                if (semicolon == std::string_view::npos) {
                    throw fastxlsx::FastXlsxError(
                        "sharedStrings reader found an unterminated XML entity reference");
                }
                const std::string_view entity =
                    text.substr(offset + 1, semicolon - offset - 1);
                if (entity == "amp") {
                    item_text_.push_back('&');
                } else if (entity == "lt") {
                    item_text_.push_back('<');
                } else if (entity == "gt") {
                    item_text_.push_back('>');
                } else if (entity == "quot") {
                    item_text_.push_back('"');
                } else if (entity == "apos") {
                    item_text_.push_back('\'');
                } else {
                    append_utf8(item_text_, parse_character_reference(entity));
                }
                offset = semicolon;
            }
            if (item_text_.size() > options_.max_item_text_bytes) {
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader exceeded max_item_text_bytes");
            }
        }
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

std::size_t find_markup_end(std::string_view xml, std::size_t open) noexcept
{
    char quote = '\0';
    for (std::size_t index = open + 1; index < xml.size(); ++index) {
        const char ch = xml[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '>') {
            return index;
        }
    }
    return std::string_view::npos;
}

std::size_t consume_available(
    std::string_view window, bool final_chunk, SharedStringProjectionReader& reader)
{
    std::size_t position = 0;
    while (position < window.size()) {
        if (window[position] != '<') {
            const std::size_t open = window.find('<', position);
            if (open == std::string_view::npos && !final_chunk) {
                return position;
            }
            const std::size_t end =
                open == std::string_view::npos ? window.size() : open;
            reader.consume_text(window.substr(position, end - position));
            position = end;
            continue;
        }

        if (window.substr(position).starts_with("<!--")) {
            const std::size_t end = window.find("-->", position + 4);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader found an unterminated XML comment");
            }
            reader.consume_special_markup();
            position = end + 3;
            continue;
        }
        if (window.substr(position).starts_with("<?")) {
            const std::size_t end = window.find("?>", position + 2);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError(
                    "sharedStrings reader found an unterminated processing instruction");
            }
            reader.consume_special_markup();
            position = end + 2;
            continue;
        }
        if (window.substr(position).starts_with("<!")) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader does not support declaration or CDATA markup");
        }

        const std::size_t close = find_markup_end(window, position);
        if (close == std::string_view::npos) {
            if (!final_chunk) {
                return position;
            }
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader found unterminated XML markup");
        }
        reader.consume_tag(window.substr(position, close + 1 - position));
        position = close + 1;
    }
    return position;
}

void process_window(std::string& window, bool final_chunk,
    SharedStringProjectionReader& reader)
{
    const std::size_t consumed = consume_available(window, final_chunk, reader);
    if (consumed != 0) {
        window.erase(0, consumed);
    }
}

void process_source_chunk(std::string_view chunk,
    fastxlsx::SharedStringReaderOptions options,
    std::string& window,
    SharedStringProjectionReader& reader)
{
    std::size_t chunk_offset = 0;
    while (chunk_offset < chunk.size()) {
        process_window(window, false, reader);
        if (window.size() >= options.max_xml_window_bytes) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader exceeded bounded input window");
        }

        const std::size_t available = options.max_xml_window_bytes - window.size();
        const std::size_t bytes_to_append =
            std::min(available, chunk.size() - chunk_offset);
        window.append(chunk.data() + chunk_offset, bytes_to_append);
        chunk_offset += bytes_to_append;
        process_window(window, false, reader);

        if (bytes_to_append == 0 && !window.empty()) {
            throw fastxlsx::FastXlsxError(
                "sharedStrings reader exceeded bounded input window");
        }
    }
}

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
    std::string window;
    window.reserve(std::min<std::size_t>(options.max_xml_window_bytes, 4096U));

    std::string chunk;
    while (read_next_chunk(chunk)) {
        process_source_chunk(chunk, options, window, reader);
    }
    process_window(window, true, reader);
    return reader.finish();
}

} // namespace fastxlsx::detail
