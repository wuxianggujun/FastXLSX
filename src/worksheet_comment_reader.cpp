#include "worksheet_comment_reader.hpp"

#include "bounded_xml_reader.hpp"

#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr std::string_view relationship_namespace =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr std::string_view spreadsheet_namespace =
    "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
constexpr std::string_view comments_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments";
constexpr std::string_view threaded_comment_relationship_type =
    "http://schemas.microsoft.com/office/2017/10/relationships/threadedComment";
constexpr std::string_view vml_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing";
constexpr std::string_view comments_content_type =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml";
constexpr std::string_view vml_content_type =
    "application/vnd.openxmlformats-officedocument.vmlDrawing";
constexpr std::uint32_t max_excel_rows = 1048576U;
constexpr std::uint32_t max_excel_columns = 16384U;

bool is_xml_space(char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\r'
        || character == '\n';
}

bool has_non_whitespace(std::string_view value) noexcept
{
    return std::any_of(value.begin(), value.end(),
        [](char character) { return !is_xml_space(character); });
}

bool is_ascii_name_boundary(char character) noexcept
{
    return is_xml_space(character) || character == '/' || character == '>'
        || character == '?';
}

struct QualifiedName {
    std::string_view prefix;
    std::string_view local_name;
};

QualifiedName parse_qualified_name(
    std::string_view name, std::string_view context)
{
    const std::size_t separator = name.find(':');
    if (separator == std::string_view::npos) {
        if (name.empty()) {
            throw FastXlsxError(
                "worksheet comments reader contains an empty "
                + std::string(context));
        }
        return QualifiedName {{}, name};
    }
    if (separator == 0 || separator + 1U == name.size()
        || name.find(':', separator + 1U) != std::string_view::npos) {
        throw FastXlsxError(
            "worksheet comments reader contains an invalid qualified "
            + std::string(context));
    }
    return QualifiedName {name.substr(0, separator),
        name.substr(separator + 1U)};
}

struct ParsedTag {
    bool closing = false;
    bool self_closing = false;
    std::string_view qualified_name;
    std::string_view local_name;
    std::string_view prefix;
    std::vector<std::pair<std::string_view, std::string_view>> attributes;
};

ParsedTag parse_tag(std::string_view raw_xml)
{
    if (raw_xml.size() < 3 || raw_xml.front() != '<' || raw_xml.back() != '>') {
        throw FastXlsxError(
            "worksheet comments reader contains an invalid XML tag");
    }
    if (raw_xml[1] == '!' || raw_xml[1] == '?') {
        throw FastXlsxError(
            "worksheet comments reader received unsupported declaration markup as a tag");
    }

    ParsedTag result;
    result.closing = raw_xml.size() > 2 && raw_xml[1] == '/';
    std::size_t position = result.closing ? 2U : 1U;
    const std::size_t name_begin = position;
    while (position < raw_xml.size()
        && !is_ascii_name_boundary(raw_xml[position])) {
        ++position;
    }
    if (position == name_begin) {
        throw FastXlsxError(
            "worksheet comments reader contains an empty element name");
    }
    result.qualified_name = raw_xml.substr(name_begin, position - name_begin);
    const QualifiedName element_name = parse_qualified_name(
        result.qualified_name, "element name");
    result.local_name = element_name.local_name;
    result.prefix = element_name.prefix;

    if (result.closing) {
        while (position < raw_xml.size() - 1U && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position != raw_xml.size() - 1U) {
            throw FastXlsxError(
                "worksheet comments reader closing tag contains attributes");
        }
        return result;
    }

    while (position < raw_xml.size()) {
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet comments reader contains an incomplete XML tag");
        }
        if (raw_xml[position] == '>') {
            if (position + 1U != raw_xml.size()) {
                throw FastXlsxError(
                    "worksheet comments reader contains trailing XML tag bytes");
            }
            return result;
        }
        if (raw_xml[position] == '/') {
            ++position;
            while (position < raw_xml.size() - 1U && is_xml_space(raw_xml[position])) {
                ++position;
            }
            if (position != raw_xml.size() - 1U || raw_xml[position] != '>') {
                throw FastXlsxError(
                    "worksheet comments reader contains an invalid self-closing tag tail");
            }
            result.self_closing = true;
            return result;
        }

        const std::size_t attribute_begin = position;
        while (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '=' && raw_xml[position] != '/'
            && raw_xml[position] != '>') {
            ++position;
        }
        if (position == attribute_begin) {
            throw FastXlsxError(
                "worksheet comments reader contains an empty attribute name");
        }
        const std::string_view attribute_name =
            raw_xml.substr(attribute_begin, position - attribute_begin);
        (void)parse_qualified_name(attribute_name, "attribute name");
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size() || raw_xml[position] != '=') {
            throw FastXlsxError(
                "worksheet comments reader contains an attribute without a value");
        }
        ++position;
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()
            || (raw_xml[position] != '"' && raw_xml[position] != '\'')) {
            throw FastXlsxError(
                "worksheet comments reader contains an unquoted attribute value");
        }
        const char quote = raw_xml[position++];
        const std::size_t value_begin = position;
        while (position < raw_xml.size() && raw_xml[position] != quote) {
            if (raw_xml[position] == '<') {
                throw FastXlsxError(
                    "worksheet comments reader attribute contains an invalid '<' byte");
            }
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet comments reader contains an unterminated attribute value");
        }
        const std::string_view attribute_value =
            raw_xml.substr(value_begin, position - value_begin);
        ++position;
        if (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '/' && raw_xml[position] != '>') {
            throw FastXlsxError(
                "worksheet comments reader attributes are not separated by whitespace");
        }
        const auto duplicate = std::find_if(result.attributes.begin(),
            result.attributes.end(), [attribute_name](const auto& attribute) {
                return attribute.first == attribute_name;
            });
        if (duplicate != result.attributes.end()) {
            throw FastXlsxError(
                "worksheet comments reader contains a duplicate attribute");
        }
        result.attributes.emplace_back(attribute_name, attribute_value);
    }

    throw FastXlsxError(
        "worksheet comments reader contains an incomplete XML tag");
}

bool is_valid_xml_code_point(std::uint32_t code_point) noexcept
{
    return code_point == 0x09U || code_point == 0x0AU || code_point == 0x0DU
        || (code_point >= 0x20U && code_point <= 0xD7FFU)
        || (code_point >= 0xE000U && code_point <= 0xFFFDU)
        || (code_point >= 0x10000U && code_point <= 0x10FFFFU);
}

void append_utf8(std::string& output, std::uint32_t code_point,
    std::size_t limit, std::string_view label)
{
    if (!is_valid_xml_code_point(code_point)) {
        throw FastXlsxError(
            "worksheet comments reader contains an invalid XML character in "
            + std::string(label));
    }
    const std::size_t byte_count = code_point <= 0x7FU ? 1U
        : (code_point <= 0x7FFU ? 2U : (code_point <= 0xFFFFU ? 3U : 4U));
    if (output.size() > limit || byte_count > limit - output.size()) {
        throw FastXlsxError(
            "worksheet comments reader " + std::string(label)
            + " exceeds its configured text limit");
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

std::uint32_t parse_entity_code_point(
    std::string_view body, std::string_view label)
{
    if (body.size() < 2 || body.front() != '#') {
        throw FastXlsxError(
            "worksheet comments reader contains an unknown XML entity in "
            + std::string(label));
    }
    std::size_t position = 1;
    std::uint32_t base = 10;
    if (position < body.size() && (body[position] == 'x' || body[position] == 'X')) {
        base = 16;
        ++position;
    }
    if (position == body.size()) {
        throw FastXlsxError(
            "worksheet comments reader contains an invalid XML entity in "
            + std::string(label));
    }
    std::uint32_t value = 0;
    for (; position < body.size(); ++position) {
        const char character = body[position];
        std::uint32_t digit = base;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint32_t>(character - '0');
        } else if (base == 16 && character >= 'a' && character <= 'f') {
            digit = static_cast<std::uint32_t>(character - 'a' + 10);
        } else if (base == 16 && character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint32_t>(character - 'A' + 10);
        }
        if (digit >= base || value > (0x10FFFFU - digit) / base) {
            throw FastXlsxError(
                "worksheet comments reader contains an invalid XML entity in "
                + std::string(label));
        }
        value = value * base + digit;
    }
    return value;
}

void append_decoded_xml(std::string& output, std::string_view raw,
    std::size_t limit, std::string_view label, bool normalize_whitespace)
{
    std::size_t position = 0;
    while (position < raw.size()) {
        if (raw[position] != '&') {
            const unsigned char byte = static_cast<unsigned char>(raw[position]);
            if (byte == 0U) {
                throw FastXlsxError(
                    "worksheet comments reader contains an invalid XML character in "
                    + std::string(label));
            }
            if (output.size() >= limit) {
                throw FastXlsxError(
                    "worksheet comments reader " + std::string(label)
                    + " exceeds its configured text limit");
            }
            output.push_back(normalize_whitespace && is_xml_space(raw[position])
                    ? ' '
                    : raw[position]);
            ++position;
            continue;
        }
        const std::size_t semicolon = raw.find(';', position + 1U);
        if (semicolon == std::string_view::npos) {
            throw FastXlsxError(
                "worksheet comments reader contains an unterminated XML entity in "
                + std::string(label));
        }
        const std::string_view body =
            raw.substr(position + 1U, semicolon - position - 1U);
        if (body == "amp") {
            append_utf8(output, '&', limit, label);
        } else if (body == "lt") {
            append_utf8(output, '<', limit, label);
        } else if (body == "gt") {
            append_utf8(output, '>', limit, label);
        } else if (body == "quot") {
            append_utf8(output, '"', limit, label);
        } else if (body == "apos") {
            append_utf8(output, '\'', limit, label);
        } else {
            append_utf8(output, parse_entity_code_point(body, label), limit, label);
        }
        position = semicolon + 1U;
    }
}

std::string decode_xml_value(
    std::string_view raw, std::size_t limit, std::string_view label)
{
    std::string output;
    output.reserve(std::min(raw.size(), limit));
    append_decoded_xml(output, raw, limit, label, true);
    return output;
}

std::uint64_t parse_unsigned_decimal(
    std::string_view value, std::string_view label)
{
    if (value.empty()) {
        throw FastXlsxError(
            "worksheet comments reader has an invalid " + std::string(label));
    }
    std::uint64_t result = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            throw FastXlsxError(
                "worksheet comments reader has an invalid " + std::string(label));
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            throw FastXlsxError(
                "worksheet comments reader has an invalid " + std::string(label));
        }
        result = result * 10U + digit;
    }
    return result;
}

bool is_namespace_declaration(std::string_view name) noexcept
{
    return name == "xmlns" || name.starts_with("xmlns:");
}

struct NamespaceChange {
    std::string prefix;
    std::optional<std::string> previous_uri;
};

class NamespaceBindings {
public:
    [[nodiscard]] std::vector<NamespaceChange> apply(
        const ParsedTag& tag, std::size_t max_bytes)
    {
        std::vector<NamespaceChange> changes;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (!is_namespace_declaration(name)) {
                continue;
            }
            const std::string_view prefix = name == "xmlns"
                ? std::string_view {}
                : name.substr(6U);
            if (name != "xmlns" && prefix.empty()) {
                throw FastXlsxError(
                    "worksheet comments reader contains an invalid namespace declaration");
            }
            const std::string uri = decode_xml_value(
                raw_value, max_bytes, "namespace URI");
            const auto previous = bindings_.find(std::string(prefix));
            changes.push_back(NamespaceChange {std::string(prefix),
                previous == bindings_.end()
                    ? std::optional<std::string> {}
                    : std::optional<std::string> {previous->second}});
            bindings_[std::string(prefix)] = uri;
        }
        return changes;
    }

    void restore(const std::vector<NamespaceChange>& changes)
    {
        for (auto item = changes.rbegin(); item != changes.rend(); ++item) {
            if (item->previous_uri.has_value()) {
                bindings_[item->prefix] = *item->previous_uri;
            } else {
                bindings_.erase(item->prefix);
            }
        }
    }

    [[nodiscard]] std::string_view resolve_element(const QualifiedName& name) const
    {
        return resolve_prefix(name.prefix, "element");
    }

    [[nodiscard]] std::string_view resolve_attribute(const QualifiedName& name) const
    {
        if (name.prefix.empty()) {
            return {};
        }
        return resolve_prefix(name.prefix, "attribute");
    }

private:
    [[nodiscard]] std::string_view resolve_prefix(
        std::string_view prefix, std::string_view context) const
    {
        const auto found = bindings_.find(std::string(prefix));
        if (found == bindings_.end() || found->second.empty()) {
            throw FastXlsxError(
                "worksheet comments reader has an unbound XML namespace prefix on "
                + std::string(context));
        }
        return found->second;
    }

    std::map<std::string, std::string, std::less<>> bindings_;
};

std::optional<int> prefix_schema_rank(std::string_view name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"sheetPr", 1}, {"dimension", 2}, {"sheetViews", 3},
        {"sheetFormatPr", 4}, {"cols", 5},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [name](const auto& value) { return value.first == name; });
    return found == std::end(ranks) ? std::nullopt
                                    : std::optional<int>(found->second);
}

std::optional<int> suffix_schema_rank(std::string_view name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"sheetCalcPr", 1}, {"sheetProtection", 2}, {"protectedRanges", 3},
        {"scenarios", 4}, {"autoFilter", 5}, {"sortState", 6},
        {"dataConsolidate", 7}, {"customSheetViews", 8}, {"mergeCells", 9},
        {"phoneticPr", 10}, {"conditionalFormatting", 11},
        {"dataValidations", 12}, {"hyperlinks", 13}, {"printOptions", 14},
        {"pageMargins", 15}, {"pageSetup", 16}, {"headerFooter", 17},
        {"rowBreaks", 18}, {"colBreaks", 19}, {"customProperties", 20},
        {"cellWatches", 21}, {"ignoredErrors", 22}, {"smartTags", 23},
        {"drawing", 24}, {"legacyDrawing", 25}, {"legacyDrawingHF", 26},
        {"picture", 27}, {"oleObjects", 28}, {"controls", 29},
        {"webPublishItems", 30}, {"tableParts", 31}, {"extLst", 32},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [name](const auto& value) { return value.first == name; });
    return found == std::end(ranks) ? std::nullopt
                                    : std::optional<int>(found->second);
}

enum class WorksheetFrameRole {
    Generic,
    LegacyDrawing,
};

struct WorksheetFrame {
    std::string qualified_name;
    WorksheetFrameRole role = WorksheetFrameRole::Generic;
    std::vector<NamespaceChange> namespace_changes;
};

class WorksheetLegacyDrawingReader {
public:
    WorksheetLegacyDrawingReader(WorksheetCommentReaderOptions options,
        WorksheetCommentReadSummary& summary)
        : options_(options)
        , summary_(summary)
    {
    }

    void consume(const WorksheetEvent& event)
    {
        switch (event.kind) {
        case WorksheetEventKind::WorksheetStart:
            consume_worksheet_start(event);
            return;
        case WorksheetEventKind::WorksheetEnd:
            consume_worksheet_end(event);
            return;
        case WorksheetEventKind::SheetDataStart:
            consume_sheet_data_start(event);
            return;
        case WorksheetEventKind::SheetDataEnd:
            consume_sheet_data_end(event);
            return;
        case WorksheetEventKind::Metadata:
            if (!inside_cell_) {
                consume_metadata(event);
            }
            return;
        case WorksheetEventKind::RawText:
            if (!inside_cell_) {
                consume_raw_text(event.raw_xml);
            }
            return;
        case WorksheetEventKind::Unsupported:
            if (inside_target()) {
                throw FastXlsxError(
                    "worksheet legacyDrawing reference contains unsupported non-element content");
            }
            return;
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::ProcessingInstruction:
            if (inside_target()) {
                throw FastXlsxError(
                    "worksheet legacyDrawing reference contains unsupported nested markup");
            }
            return;
        case WorksheetEventKind::XmlDeclaration:
        case WorksheetEventKind::RowStart:
        case WorksheetEventKind::RowEnd:
        case WorksheetEventKind::CellValueMarkup:
        case WorksheetEventKind::CellValue:
            return;
        case WorksheetEventKind::CellStart:
            inside_cell_ = true;
            return;
        case WorksheetEventKind::CellEnd:
            inside_cell_ = false;
            return;
        }
    }

    [[nodiscard]] std::optional<std::string> finish()
    {
        if (!saw_worksheet_start_ || !saw_sheet_data_start_
            || !saw_sheet_data_end_ || !saw_worksheet_end_) {
            throw FastXlsxError(
                "worksheet comments reader requires a worksheet root and closed sheetData");
        }
        if (!stack_.empty()) {
            throw FastXlsxError(
                "worksheet comments reader ended inside an open worksheet element");
        }
        return std::move(legacy_drawing_relationship_id_);
    }

private:
    void consume_worksheet_start(const WorksheetEvent& event)
    {
        if (saw_worksheet_start_) {
            throw FastXlsxError(
                "worksheet comments reader found duplicate worksheet roots");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.self_closing || tag.local_name != "worksheet") {
            throw FastXlsxError(
                "worksheet comments reader found an invalid worksheet root");
        }
        root_namespace_changes_ = namespaces_.apply(
            tag, options_.max_xml_window_bytes);
        if (namespaces_.resolve_element(QualifiedName {tag.prefix, tag.local_name})
            != spreadsheet_namespace) {
            throw FastXlsxError(
                "worksheet comments reader requires the spreadsheet namespace on worksheet");
        }
        root_qualified_name_ = std::string(tag.qualified_name);
        saw_worksheet_start_ = true;
    }

    void consume_worksheet_end(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.self_closing || !tag.closing
            || tag.qualified_name != root_qualified_name_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet comments reader worksheet root QName is mismatched");
        }
        saw_worksheet_end_ = true;
        namespaces_.restore(root_namespace_changes_);
    }

    void consume_sheet_data_start(const WorksheetEvent& event)
    {
        if (!saw_worksheet_start_ || saw_sheet_data_start_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet comments reader found an invalid sheetData boundary");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "sheetData") {
            throw FastXlsxError(
                "worksheet comments reader found an invalid sheetData start");
        }
        if (std::any_of(tag.attributes.begin(), tag.attributes.end(),
                [](const auto& attribute) {
                    return is_namespace_declaration(attribute.first);
                })) {
            throw FastXlsxError(
                "worksheet comments reader does not support namespace declarations on sheetData");
        }
        if (namespaces_.resolve_element(QualifiedName {tag.prefix, tag.local_name})
            != spreadsheet_namespace) {
            throw FastXlsxError(
                "worksheet comments reader sheetData is not in the spreadsheet namespace");
        }
        saw_sheet_data_start_ = true;
    }

    void consume_sheet_data_end(const WorksheetEvent& event)
    {
        if (!saw_sheet_data_start_ || saw_sheet_data_end_) {
            throw FastXlsxError(
                "worksheet comments reader found a duplicate sheetData end");
        }
        if (!event.self_closing) {
            const ParsedTag tag = parse_tag(event.raw_xml);
            if (!tag.closing || tag.local_name != "sheetData"
                || namespaces_.resolve_element(
                       QualifiedName {tag.prefix, tag.local_name})
                    != spreadsheet_namespace) {
                throw FastXlsxError(
                    "worksheet comments reader sheetData QName is mismatched");
            }
        }
        saw_sheet_data_end_ = true;
    }

    void consume_raw_text(std::string_view text) const
    {
        if (text.empty() || !has_non_whitespace(text)) {
            return;
        }
        if (inside_target()) {
            throw FastXlsxError(
                "worksheet legacyDrawing reference contains unexpected text");
        }
        if (stack_.empty()) {
            throw FastXlsxError(
                "worksheet comments reader found unexpected worksheet text");
        }
    }

    [[nodiscard]] bool inside_target() const noexcept
    {
        return std::any_of(stack_.begin(), stack_.end(),
            [](const WorksheetFrame& frame) {
                return frame.role == WorksheetFrameRole::LegacyDrawing;
            });
    }

    void consume_metadata(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.local_name != event.element_name) {
            throw FastXlsxError(
                "worksheet comments reader element local name is mismatched");
        }
        if (tag.closing) {
            close_metadata(tag);
        } else {
            open_metadata(tag);
        }
    }

    void close_metadata(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().qualified_name != tag.qualified_name) {
            throw FastXlsxError(
                "worksheet comments reader contains mismatched element QName nesting");
        }
        WorksheetFrame frame = std::move(stack_.back());
        stack_.pop_back();
        namespaces_.restore(frame.namespace_changes);
    }

    void enforce_top_level_schema(
        const ParsedTag& tag, std::string_view namespace_uri)
    {
        if (!stack_.empty()) {
            return;
        }
        if (namespace_uri != spreadsheet_namespace) {
            throw FastXlsxError(
                "worksheet comments reader found a foreign top-level worksheet element");
        }
        if (!saw_sheet_data_start_) {
            const std::optional<int> rank = prefix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet comments reader found unsupported metadata before sheetData");
            }
            if (*rank < last_prefix_rank_) {
                throw FastXlsxError(
                    "worksheet comments reader prefix elements are not in schema order");
            }
            last_prefix_rank_ = *rank;
            return;
        }
        if (!saw_sheet_data_end_) {
            throw FastXlsxError(
                "worksheet legacyDrawing appears inside sheetData");
        }
        const std::optional<int> rank = suffix_schema_rank(tag.local_name);
        if (!rank.has_value()) {
            throw FastXlsxError(
                "worksheet comments reader found unsupported worksheet suffix metadata");
        }
        if (*rank < last_suffix_rank_) {
            throw FastXlsxError(
                "worksheet comments reader suffix elements are not in schema order");
        }
        last_suffix_rank_ = *rank;
    }

    void open_metadata(const ParsedTag& tag)
    {
        std::vector<NamespaceChange> namespace_changes =
            namespaces_.apply(tag, options_.max_xml_window_bytes);
        const std::string_view namespace_uri = namespaces_.resolve_element(
            QualifiedName {tag.prefix, tag.local_name});
        enforce_top_level_schema(tag, namespace_uri);
        if (namespace_uri == spreadsheet_namespace
            && tag.local_name == "legacyDrawing") {
            open_legacy_drawing(tag, std::move(namespace_changes));
            return;
        }
        if (inside_target()) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError(
                "worksheet legacyDrawing reference contains an unsupported child element");
        }
        open_generic(tag, std::move(namespace_changes));
    }

    void open_legacy_drawing(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (!stack_.empty() || legacy_drawing_relationship_id_.has_value()) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError(
                "worksheet comments reader found duplicate or nested legacyDrawing references");
        }
        std::optional<std::string> relationship_id;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name =
                parse_qualified_name(name, "legacyDrawing attribute name");
            if (attribute_name.local_name != "id"
                || namespaces_.resolve_attribute(attribute_name)
                    != relationship_namespace
                || relationship_id.has_value()) {
                namespaces_.restore(namespace_changes);
                throw FastXlsxError(
                    "worksheet legacyDrawing requires one OpenXML relationship id");
            }
            relationship_id = decode_xml_value(raw_value,
                options_.max_relationship_id_bytes,
                "relationship id (max_relationship_id_bytes)");
        }
        if (!relationship_id.has_value() || relationship_id->empty()) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError(
                "worksheet legacyDrawing relationship id cannot be empty");
        }
        summary_.peak_relationship_id_bytes = std::max(
            summary_.peak_relationship_id_bytes, relationship_id->size());
        legacy_drawing_relationship_id_ = std::move(*relationship_id);
        if (tag.self_closing) {
            namespaces_.restore(namespace_changes);
            return;
        }
        push_frame(tag, WorksheetFrameRole::LegacyDrawing,
            std::move(namespace_changes));
    }

    void open_generic(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (tag.self_closing) {
            namespaces_.restore(namespace_changes);
            return;
        }
        push_frame(tag, WorksheetFrameRole::Generic,
            std::move(namespace_changes));
    }

    void push_frame(const ParsedTag& tag, WorksheetFrameRole role,
        std::vector<NamespaceChange> namespace_changes)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError(
                "worksheet comments reader exceeds max_xml_nesting_depth");
        }
        stack_.push_back(WorksheetFrame {std::string(tag.qualified_name), role,
            std::move(namespace_changes)});
        summary_.peak_xml_nesting_depth = std::max(
            summary_.peak_xml_nesting_depth, stack_.size());
    }

    WorksheetCommentReaderOptions options_;
    WorksheetCommentReadSummary& summary_;
    NamespaceBindings namespaces_;
    std::vector<WorksheetFrame> stack_;
    std::vector<NamespaceChange> root_namespace_changes_;
    std::optional<std::string> legacy_drawing_relationship_id_;
    std::string root_qualified_name_;
    int last_prefix_rank_ = 0;
    int last_suffix_rank_ = 0;
    bool saw_worksheet_start_ = false;
    bool saw_sheet_data_start_ = false;
    bool saw_sheet_data_end_ = false;
    bool saw_worksheet_end_ = false;
    bool inside_cell_ = false;
};

struct A1Coordinate {
    std::uint32_t row = 1;
    std::uint32_t column = 1;
};

std::optional<A1Coordinate> parse_a1_coordinate(std::string_view reference)
{
    if (reference.empty()) {
        return std::nullopt;
    }
    std::size_t position = 0;
    std::uint64_t column = 0;
    while (position < reference.size()
        && reference[position] >= 'A' && reference[position] <= 'Z') {
        column = column * 26U
            + static_cast<std::uint64_t>(reference[position] - 'A' + 1);
        if (column > max_excel_columns) {
            return std::nullopt;
        }
        ++position;
    }
    if (position == 0 || position == reference.size()
        || reference[position] == '0') {
        return std::nullopt;
    }
    std::uint64_t row = 0;
    for (; position < reference.size(); ++position) {
        const char character = reference[position];
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        row = row * 10U + static_cast<std::uint64_t>(character - '0');
        if (row > max_excel_rows) {
            return std::nullopt;
        }
    }
    if (row == 0) {
        return std::nullopt;
    }
    return A1Coordinate {
        static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)};
}

enum class CommentRole {
    Root,
    Authors,
    Author,
    CommentList,
    Comment,
    Text,
    TextValue,
};

struct CommentFrame {
    std::string qualified_name;
    CommentRole role = CommentRole::Root;
    std::size_t child_count = 0;
    std::vector<NamespaceChange> namespace_changes;
};

class CommentProjectionReader {
public:
    CommentProjectionReader(const WorksheetCommentReadCallbacks& callbacks,
        WorksheetCommentReaderOptions options,
        WorksheetCommentReadSummary& summary)
        : callbacks_(callbacks)
        , options_(options)
        , summary_(summary)
    {
    }

    void consume_text(std::string_view text)
    {
        std::size_t offset = 0;
        if (!saw_root_ && text.substr(0, 3) == "\xef\xbb\xbf") {
            offset = 3;
        }
        text.remove_prefix(offset);
        if (stack_.empty()) {
            if (has_non_whitespace(text)) {
                throw FastXlsxError(
                    "worksheet comments part contains unexpected text outside its root");
            }
            return;
        }
        if (stack_.back().role == CommentRole::Author) {
            append_decoded_xml(current_author_, text,
                options_.max_author_bytes,
                "author (max_author_bytes)", false);
            summary_.peak_author_bytes = std::max(
                summary_.peak_author_bytes, current_author_.size());
            return;
        }
        if (stack_.back().role == CommentRole::TextValue) {
            append_decoded_xml(current_view_.text, text,
                options_.max_comment_text_bytes,
                "comment text (max_comment_text_bytes)", false);
            summary_.peak_comment_text_bytes = std::max(
                summary_.peak_comment_text_bytes, current_view_.text.size());
            return;
        }
        if (has_non_whitespace(text)) {
            throw FastXlsxError(
                "worksheet comments part contains unexpected element text");
        }
    }

    void consume_special_markup() const
    {
        if (saw_root_ && !finished_root_) {
            throw FastXlsxError(
                "worksheet comments part contains unsupported nested markup");
        }
    }

    void consume_tag(std::string_view raw_tag)
    {
        const ParsedTag tag = parse_tag(raw_tag);
        if (tag.closing) {
            close_tag(tag);
        } else {
            open_tag(tag);
        }
    }

    void finish() const
    {
        if (!saw_root_ || !finished_root_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet comments reader requires one closed comments root");
        }
    }

private:
    void open_tag(const ParsedTag& tag)
    {
        if (!saw_root_) {
            open_root(tag);
            return;
        }
        if (finished_root_ || stack_.empty()) {
            throw FastXlsxError(
                "worksheet comments part contains multiple root elements");
        }
        if (std::any_of(tag.attributes.begin(), tag.attributes.end(),
                [](const auto& attribute) {
                    return is_namespace_declaration(attribute.first);
                })) {
            throw FastXlsxError(
                "worksheet comments part does not support nested namespace declarations");
        }
        const std::vector<NamespaceChange> namespace_changes =
            namespaces_.apply(tag, options_.max_xml_window_bytes);
        const std::string_view namespace_uri = namespaces_.resolve_element(
            QualifiedName {tag.prefix, tag.local_name});
        CommentFrame& parent = stack_.back();
        const CommentRole role = child_role(parent, namespace_uri, tag.local_name);
        ++parent.child_count;
        begin_role(role, tag);

        if (tag.self_closing) {
            if (role != CommentRole::Author && role != CommentRole::TextValue) {
                namespaces_.restore(namespace_changes);
                throw FastXlsxError(
                    "worksheet comments part contains a self-closing structural element");
            }
            finish_role(role, 0);
            namespaces_.restore(namespace_changes);
            return;
        }
        push_frame(tag, role, namespace_changes);
    }

    void open_root(const ParsedTag& tag)
    {
        if (tag.closing || tag.self_closing || tag.local_name != "comments") {
            throw FastXlsxError(
                "worksheet comments reader requires a nonempty comments root");
        }
        const std::vector<NamespaceChange> namespace_changes =
            namespaces_.apply(tag, options_.max_xml_window_bytes);
        for (const auto& [name, _] : tag.attributes) {
            if (!is_namespace_declaration(name)) {
                namespaces_.restore(namespace_changes);
                throw FastXlsxError(
                    "worksheet comments root has unsupported metadata");
            }
        }
        if (namespaces_.resolve_element(QualifiedName {tag.prefix, tag.local_name})
            != spreadsheet_namespace) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError(
                "worksheet comments root is not in the spreadsheet namespace");
        }
        saw_root_ = true;
        push_frame(tag, CommentRole::Root, namespace_changes);
    }

    void close_tag(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().qualified_name != tag.qualified_name) {
            throw FastXlsxError(
                "worksheet comments part contains mismatched element QName nesting");
        }
        const CommentRole role = stack_.back().role;
        finish_role(role, stack_.back().child_count);
        CommentFrame frame = std::move(stack_.back());
        stack_.pop_back();
        namespaces_.restore(frame.namespace_changes);
        if (role == CommentRole::Root) {
            finished_root_ = true;
        }
    }

    [[nodiscard]] CommentRole child_role(const CommentFrame& parent,
        std::string_view namespace_uri, std::string_view local_name) const
    {
        if (namespace_uri != spreadsheet_namespace) {
            throw FastXlsxError(
                "worksheet comments part contains a foreign child element");
        }
        switch (parent.role) {
        case CommentRole::Root:
            if (parent.child_count == 0 && local_name == "authors") {
                return CommentRole::Authors;
            }
            if (parent.child_count == 1 && local_name == "commentList") {
                return CommentRole::CommentList;
            }
            throw FastXlsxError(
                "worksheet comments root children are incomplete or out of schema order");
        case CommentRole::Authors:
            if (local_name == "author") {
                return CommentRole::Author;
            }
            break;
        case CommentRole::CommentList:
            if (local_name == "comment") {
                return CommentRole::Comment;
            }
            break;
        case CommentRole::Comment:
            if (parent.child_count == 0 && local_name == "text") {
                return CommentRole::Text;
            }
            break;
        case CommentRole::Text:
            if (parent.child_count == 0 && local_name == "t") {
                return CommentRole::TextValue;
            }
            if (local_name == "r") {
                throw FastXlsxError(
                    "worksheet comments reader does not project rich text runs");
            }
            if (local_name == "rPh" || local_name == "phoneticPr") {
                throw FastXlsxError(
                    "worksheet comments reader does not project phonetic metadata");
            }
            if (local_name == "extLst" || local_name == "ext") {
                throw FastXlsxError(
                    "worksheet comments reader does not project extension metadata");
            }
            break;
        case CommentRole::Author:
        case CommentRole::TextValue:
            break;
        }
        throw FastXlsxError(
            "worksheet comments part contains an unsupported child element");
    }

    void begin_role(CommentRole role, const ParsedTag& tag)
    {
        switch (role) {
        case CommentRole::Author:
            require_no_attributes(tag, "author");
            if (authors_.size() >= options_.max_author_count) {
                throw FastXlsxError(
                    "worksheet comments reader exceeds max_author_count");
            }
            current_author_.clear();
            return;
        case CommentRole::Comment:
            begin_comment(tag);
            return;
        case CommentRole::TextValue:
            validate_text_attributes(tag);
            return;
        case CommentRole::Authors:
        case CommentRole::CommentList:
        case CommentRole::Text:
            require_no_attributes(tag, "structural element");
            return;
        case CommentRole::Root:
            break;
        }
        throw FastXlsxError(
            "worksheet comments reader entered an invalid element role");
    }

    void require_no_attributes(
        const ParsedTag& tag, std::string_view context) const
    {
        if (!tag.attributes.empty()) {
            throw FastXlsxError(
                "worksheet comments reader found unsupported "
                + std::string(context) + " metadata");
        }
    }

    void validate_text_attributes(const ParsedTag& tag) const
    {
        bool saw_space = false;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (name != "xml:space" || saw_space
                || (raw_value != "preserve" && raw_value != "default")) {
                throw FastXlsxError(
                    "worksheet comments reader found unsupported text metadata");
            }
            saw_space = true;
        }
    }

    void begin_comment(const ParsedTag& tag)
    {
        if (summary_.comment_count >= options_.max_comment_count) {
            throw FastXlsxError(
                "worksheet comments reader exceeds max_comment_count");
        }
        std::optional<std::string> reference;
        std::optional<std::string> author_id_text;
        std::optional<std::string> shape_id_text;
        for (const auto& [name, raw_value] : tag.attributes) {
            const QualifiedName attribute_name =
                parse_qualified_name(name, "comment attribute name");
            if (!attribute_name.prefix.empty()) {
                throw FastXlsxError(
                    "worksheet comments reader found a qualified comment attribute");
            }
            if (attribute_name.local_name == "ref" && !reference.has_value()) {
                reference = decode_xml_value(raw_value,
                    options_.max_cell_reference_bytes,
                    "cell reference (max_cell_reference_bytes)");
            } else if (attribute_name.local_name == "authorId"
                && !author_id_text.has_value()) {
                author_id_text = decode_xml_value(raw_value,
                    options_.max_author_id_bytes,
                    "authorId (max_author_id_bytes)");
            } else if (attribute_name.local_name == "shapeId"
                && !shape_id_text.has_value()) {
                shape_id_text = decode_xml_value(raw_value,
                    options_.max_shape_id_bytes,
                    "shapeId (max_shape_id_bytes)");
            } else {
                throw FastXlsxError(
                    "worksheet comments reader found unsupported comment metadata");
            }
        }
        if (!reference.has_value() || !author_id_text.has_value()) {
            throw FastXlsxError(
                "worksheet comments reader requires ref and authorId");
        }
        summary_.peak_cell_reference_bytes = std::max(
            summary_.peak_cell_reference_bytes, reference->size());
        summary_.peak_author_id_bytes = std::max(
            summary_.peak_author_id_bytes, author_id_text->size());
        const std::optional<A1Coordinate> coordinate =
            parse_a1_coordinate(*reference);
        if (!coordinate.has_value()) {
            throw FastXlsxError(
                "worksheet comments reader requires one valid uppercase A1 cell reference");
        }
        const std::uint64_t author_id = parse_unsigned_decimal(
            *author_id_text, "authorId");
        if (author_id >= authors_.size()) {
            throw FastXlsxError(
                "worksheet comments reader authorId is outside the author table");
        }
        if (shape_id_text.has_value()) {
            summary_.peak_shape_id_bytes = std::max(
                summary_.peak_shape_id_bytes, shape_id_text->size());
            const std::uint64_t shape_id = parse_unsigned_decimal(
                *shape_id_text, "shapeId");
            if (shape_id > std::numeric_limits<std::uint32_t>::max()) {
                throw FastXlsxError(
                    "worksheet comments reader shapeId exceeds uint32");
            }
            ++summary_.comment_with_shape_id_count;
        }
        const auto key = std::pair {
            coordinate->row, coordinate->column};
        if (!comment_references_.insert(key).second) {
            throw FastXlsxError(
                "worksheet comments reader found a duplicate cell reference");
        }
        summary_.peak_retained_comment_reference_count = std::max(
            summary_.peak_retained_comment_reference_count,
            comment_references_.size());
        current_view_ = WorksheetCommentView {};
        current_view_.index = summary_.comment_count;
        current_view_.row = coordinate->row;
        current_view_.column = coordinate->column;
        current_view_.author = authors_[static_cast<std::size_t>(author_id)];
    }

    void finish_role(CommentRole role, std::size_t child_count)
    {
        switch (role) {
        case CommentRole::Root:
            require_child_count(child_count, 2, "comments root");
            return;
        case CommentRole::Authors:
            if (child_count == 0) {
                throw FastXlsxError(
                    "worksheet comments reader requires at least one author");
            }
            return;
        case CommentRole::Author:
            require_child_count(child_count, 0, "author");
            retain_author();
            return;
        case CommentRole::CommentList:
            if (child_count == 0) {
                throw FastXlsxError(
                    "worksheet comments reader requires at least one comment");
            }
            return;
        case CommentRole::Comment:
            require_child_count(child_count, 1, "comment");
            emit_comment();
            return;
        case CommentRole::Text:
            require_child_count(child_count, 1, "comment text");
            return;
        case CommentRole::TextValue:
            require_child_count(child_count, 0, "simple text value");
            return;
        }
    }

    void require_child_count(std::size_t actual, std::size_t expected,
        std::string_view context) const
    {
        if (actual != expected) {
            throw FastXlsxError(
                "worksheet comments reader found an incomplete "
                + std::string(context));
        }
    }

    void retain_author()
    {
        if (current_author_.size()
            > options_.max_total_author_bytes - summary_.total_author_bytes) {
            throw FastXlsxError(
                "worksheet comments reader exceeds max_total_author_bytes");
        }
        summary_.total_author_bytes += current_author_.size();
        authors_.push_back(std::move(current_author_));
        current_author_.clear();
        summary_.author_count = authors_.size();
        summary_.peak_retained_author_count = std::max(
            summary_.peak_retained_author_count, authors_.size());
    }

    void emit_comment()
    {
        if (callbacks_.on_comment) {
            callbacks_.on_comment(current_view_);
        }
        ++summary_.comment_count;
        current_view_ = WorksheetCommentView {};
    }

    void push_frame(const ParsedTag& tag, CommentRole role,
        const std::vector<NamespaceChange>& namespace_changes)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError(
                "worksheet comments reader exceeds max_xml_nesting_depth");
        }
        stack_.push_back(CommentFrame {std::string(tag.qualified_name), role, 0,
            namespace_changes});
        summary_.peak_xml_nesting_depth = std::max(
            summary_.peak_xml_nesting_depth, stack_.size());
    }

    const WorksheetCommentReadCallbacks& callbacks_;
    WorksheetCommentReaderOptions options_;
    WorksheetCommentReadSummary& summary_;
    NamespaceBindings namespaces_;
    std::vector<CommentFrame> stack_;
    std::vector<std::string> authors_;
    std::set<std::pair<std::uint32_t, std::uint32_t>> comment_references_;
    WorksheetCommentView current_view_;
    std::string current_author_;
    bool saw_root_ = false;
    bool finished_root_ = false;
};

int hex_digit_value(char character) noexcept
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

std::string decode_relationship_target(
    std::string_view target, const WorksheetCommentReaderOptions& options,
    WorksheetCommentReadSummary& summary)
{
    if (target.empty()) {
        throw FastXlsxError(
            "worksheet comments relationship target cannot be empty");
    }
    if (target.size() > options.max_relationship_target_bytes) {
        throw FastXlsxError(
            "worksheet comments relationship target exceeds max_relationship_target_bytes");
    }
    std::string decoded;
    decoded.reserve(target.size());
    for (std::size_t index = 0; index < target.size(); ++index) {
        if (target[index] != '%') {
            if (target[index] == '\0') {
                throw FastXlsxError(
                    "worksheet comments relationship target contains a null byte");
            }
            decoded.push_back(target[index]);
            continue;
        }
        if (index + 2U >= target.size()) {
            throw FastXlsxError(
                "worksheet comments relationship target has invalid percent encoding");
        }
        const int high = hex_digit_value(target[index + 1U]);
        const int low = hex_digit_value(target[index + 2U]);
        if (high < 0 || low < 0) {
            throw FastXlsxError(
                "worksheet comments relationship target has invalid percent encoding");
        }
        const char decoded_byte = static_cast<char>((high << 4) | low);
        if (decoded_byte == '\0') {
            throw FastXlsxError(
                "worksheet comments relationship target decodes to a null byte");
        }
        decoded.push_back(decoded_byte);
        index += 2U;
    }
    if (decoded.size() > options.max_relationship_target_bytes) {
        throw FastXlsxError(
            "worksheet comments relationship target exceeds max_relationship_target_bytes");
    }
    summary.peak_relationship_target_bytes = std::max(
        summary.peak_relationship_target_bytes, decoded.size());
    return decoded;
}

PartName resolve_relationship_part(const PartName& owner_part,
    const Relationship& relationship, const WorksheetCommentReaderOptions& options,
    WorksheetCommentReadSummary& summary, std::string_view relationship_name)
{
    std::string target = decode_relationship_target(
        relationship.target, options, summary);
    if (target.find_first_of("?#") != std::string::npos) {
        throw FastXlsxError(
            "worksheet " + std::string(relationship_name)
            + " relationship target cannot contain a query or fragment");
    }
    if (!target.empty() && target.front() == '/') {
        return PartName(target);
    }
    const std::string source = owner_part.value();
    const std::size_t slash = source.find_last_of('/');
    if (slash == std::string::npos) {
        throw FastXlsxError(
            "worksheet comments relationship owner has no package directory");
    }
    return PartName(source.substr(0, slash) + "/" + target);
}

void validate_options(const WorksheetCommentReaderOptions& options)
{
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_comment_count == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_comment_count");
    }
    if (options.max_author_count == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_author_count");
    }
    if (options.max_author_bytes == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_author_bytes");
    }
    if (options.max_total_author_bytes == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_total_author_bytes");
    }
    if (options.max_comment_text_bytes == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_comment_text_bytes");
    }
    if (options.max_cell_reference_bytes == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_cell_reference_bytes");
    }
    if (options.max_author_id_bytes == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_author_id_bytes");
    }
    if (options.max_relationship_id_bytes == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_relationship_id_bytes");
    }
    if (options.max_relationship_target_bytes == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_relationship_target_bytes");
    }
    if (options.max_shape_id_bytes == 0) {
        throw FastXlsxError(
            "WorksheetCommentReader requires nonzero max_shape_id_bytes");
    }
}

const Relationship* find_comments_relationship(
    const RelationshipSet* relationships)
{
    if (relationships == nullptr) {
        return nullptr;
    }
    const Relationship* comments = nullptr;
    for (const Relationship& relationship : relationships->relationships()) {
        if (relationship.type == threaded_comment_relationship_type) {
            throw FastXlsxError(
                "worksheet comments reader does not project threaded comments or persons");
        }
        if (relationship.type != comments_relationship_type) {
            continue;
        }
        if (comments != nullptr) {
            throw FastXlsxError(
                "worksheet comments reader found duplicate comments relationships");
        }
        comments = &relationship;
    }
    return comments;
}

void audit_legacy_drawing(const PackageReader& package,
    const PartName& worksheet_part, const RelationshipSet& relationships,
    std::string_view relationship_id,
    const WorksheetCommentReaderOptions& options,
    WorksheetCommentReadSummary& summary)
{
    const Relationship* relationship = relationships.find_by_id(relationship_id);
    if (relationship == nullptr) {
        throw FastXlsxError(
            "worksheet legacyDrawing relationship id is missing");
    }
    if (relationship->type != vml_relationship_type) {
        throw FastXlsxError(
            "worksheet legacyDrawing relationship has the wrong type");
    }
    if (relationship->target_mode != Relationship::TargetMode::Internal) {
        throw FastXlsxError(
            "worksheet legacyDrawing relationship must be internal");
    }
    const PartName vml_part = resolve_relationship_part(worksheet_part,
        *relationship, options, summary, "legacyDrawing");
    const PackagePart* indexed_part = package.part_index().find_part(vml_part);
    if (indexed_part == nullptr) {
        throw FastXlsxError(
            "worksheet legacyDrawing relationship targets an unknown part");
    }
    if (indexed_part->content_type != vml_content_type) {
        throw FastXlsxError(
            "worksheet legacyDrawing target has the wrong content type");
    }
    if (package.find_entry(vml_part.zip_path()) == nullptr) {
        throw FastXlsxError(
            "worksheet legacyDrawing part has no ZIP entry");
    }
    summary.has_legacy_drawing = true;
}

} // namespace

WorksheetCommentReadSummary read_worksheet_comments_from_package(
    const PackageReader& package,
    const PartName& worksheet_part,
    const WorksheetCommentReadCallbacks& callbacks,
    WorksheetCommentReaderOptions options)
{
    validate_options(options);

    WorksheetCommentReadSummary summary;
    const RelationshipSet* worksheet_relationships =
        package.relationships_for(worksheet_part);
    const Relationship* comments_relationship =
        find_comments_relationship(worksheet_relationships);
    if (comments_relationship == nullptr) {
        return summary;
    }
    if (comments_relationship->target_mode != Relationship::TargetMode::Internal) {
        throw FastXlsxError(
            "worksheet comments relationship must be internal");
    }
    const PartName comments_part = resolve_relationship_part(worksheet_part,
        *comments_relationship, options, summary, "comments");
    const PackagePart* indexed_comments_part =
        package.part_index().find_part(comments_part);
    if (indexed_comments_part == nullptr) {
        throw FastXlsxError(
            "worksheet comments relationship targets an unknown part");
    }
    if (indexed_comments_part->content_type != comments_content_type) {
        throw FastXlsxError(
            "worksheet comments target has the wrong content type");
    }
    if (package.find_entry(comments_part.zip_path()) == nullptr) {
        throw FastXlsxError(
            "worksheet comments part has no ZIP entry");
    }

    WorksheetLegacyDrawingReader worksheet_reader(options, summary);
    WorksheetEventReaderOptions event_options;
    event_options.max_window_bytes = options.max_xml_window_bytes;
    event_options.copy_context_attributes = false;
    scan_worksheet_events_from_chunk_source(
        package.entry_chunk_source(worksheet_part.zip_path()),
        [&worksheet_reader](const WorksheetEvent& event) {
            worksheet_reader.consume(event);
        },
        event_options);
    const std::optional<std::string> legacy_drawing_relationship_id =
        worksheet_reader.finish();
    if (legacy_drawing_relationship_id.has_value()) {
        audit_legacy_drawing(package, worksheet_part, *worksheet_relationships,
            *legacy_drawing_relationship_id, options, summary);
    }

    CommentProjectionReader comments_reader(callbacks, options, summary);
    BoundedXmlCallbacks xml_callbacks;
    xml_callbacks.on_text = [&comments_reader](std::string_view text) {
        comments_reader.consume_text(text);
    };
    xml_callbacks.on_tag = [&comments_reader](std::string_view tag) {
        comments_reader.consume_tag(tag);
    };
    xml_callbacks.on_special_markup = [&comments_reader] {
        comments_reader.consume_special_markup();
    };
    scan_bounded_xml_from_chunk_source(
        package.entry_chunk_source(comments_part.zip_path()), xml_callbacks,
        options.max_xml_window_bytes, "worksheet comments");
    comments_reader.finish();
    return summary;
}

} // namespace fastxlsx::detail
