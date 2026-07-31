#include "worksheet_image_reader.hpp"

#include "bounded_xml_reader.hpp"
#include "package_reader.hpp"

#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
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
constexpr std::string_view drawing_namespace =
    "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing";
constexpr std::string_view drawing_main_namespace =
    "http://schemas.openxmlformats.org/drawingml/2006/main";
constexpr std::string_view worksheet_drawing_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
constexpr std::string_view image_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
constexpr std::string_view drawing_content_type =
    "application/vnd.openxmlformats-officedocument.drawing+xml";
constexpr std::string_view png_content_type = "image/png";
constexpr std::string_view jpeg_content_type = "image/jpeg";
constexpr std::uint32_t max_excel_rows = 1048576U;
constexpr std::uint32_t max_excel_columns = 16384U;
constexpr std::uint64_t max_openxml_coordinate = 27273042316900ULL;

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

std::string_view trim(std::string_view value) noexcept
{
    while (!value.empty() && is_xml_space(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && is_xml_space(value.back())) {
        value.remove_suffix(1);
    }
    return value;
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
                "worksheet image reader contains an empty " + std::string(context));
        }
        return QualifiedName {{}, name};
    }
    if (separator == 0 || separator + 1U == name.size()
        || name.find(':', separator + 1U) != std::string_view::npos) {
        throw FastXlsxError(
            "worksheet image reader contains an invalid qualified "
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
        throw FastXlsxError("worksheet image reader contains an invalid XML tag");
    }
    if (raw_xml[1] == '!' || raw_xml[1] == '?') {
        throw FastXlsxError(
            "worksheet image reader received unsupported declaration markup as a tag");
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
        throw FastXlsxError("worksheet image reader contains an empty element name");
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
                "worksheet image reader closing tag contains attributes");
        }
        return result;
    }

    while (position < raw_xml.size()) {
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError("worksheet image reader contains an incomplete XML tag");
        }
        if (raw_xml[position] == '>') {
            if (position + 1U != raw_xml.size()) {
                throw FastXlsxError(
                    "worksheet image reader contains trailing XML tag bytes");
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
                    "worksheet image reader contains an invalid self-closing tag tail");
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
                "worksheet image reader contains an empty attribute name");
        }
        const std::string_view attribute_name =
            raw_xml.substr(attribute_begin, position - attribute_begin);
        (void)parse_qualified_name(attribute_name, "attribute name");
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size() || raw_xml[position] != '=') {
            throw FastXlsxError(
                "worksheet image reader contains an attribute without a value");
        }
        ++position;
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()
            || (raw_xml[position] != '"' && raw_xml[position] != '\'')) {
            throw FastXlsxError(
                "worksheet image reader contains an unquoted attribute value");
        }
        const char quote = raw_xml[position++];
        const std::size_t value_begin = position;
        while (position < raw_xml.size() && raw_xml[position] != quote) {
            if (raw_xml[position] == '<') {
                throw FastXlsxError(
                    "worksheet image reader attribute contains an invalid '<' byte");
            }
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet image reader contains an unterminated attribute value");
        }
        const std::string_view attribute_value =
            raw_xml.substr(value_begin, position - value_begin);
        ++position;
        if (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '/' && raw_xml[position] != '>') {
            throw FastXlsxError(
                "worksheet image reader attributes are not separated by whitespace");
        }
        const auto duplicate = std::find_if(result.attributes.begin(),
            result.attributes.end(), [attribute_name](const auto& attribute) {
                return attribute.first == attribute_name;
            });
        if (duplicate != result.attributes.end()) {
            throw FastXlsxError(
                "worksheet image reader contains a duplicate attribute");
        }
        result.attributes.emplace_back(attribute_name, attribute_value);
    }

    throw FastXlsxError("worksheet image reader contains an incomplete XML tag");
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
            "worksheet image reader contains an invalid XML character in "
            + std::string(label));
    }
    const std::size_t byte_count = code_point <= 0x7FU ? 1U
        : (code_point <= 0x7FFU ? 2U : (code_point <= 0xFFFFU ? 3U : 4U));
    if (output.size() > limit || byte_count > limit - output.size()) {
        throw FastXlsxError(
            "worksheet image reader " + std::string(label)
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
            "worksheet image reader contains an unknown XML entity in "
            + std::string(label));
    }
    std::size_t position = 1;
    int base = 10;
    if (position < body.size() && (body[position] == 'x' || body[position] == 'X')) {
        base = 16;
        ++position;
    }
    if (position == body.size()) {
        throw FastXlsxError(
            "worksheet image reader contains an invalid XML entity in "
            + std::string(label));
    }
    std::uint32_t value = 0;
    for (; position < body.size(); ++position) {
        const char character = body[position];
        int digit = -1;
        if (character >= '0' && character <= '9') {
            digit = character - '0';
        } else if (base == 16 && character >= 'a' && character <= 'f') {
            digit = character - 'a' + 10;
        } else if (base == 16 && character >= 'A' && character <= 'F') {
            digit = character - 'A' + 10;
        }
        if (digit < 0 || digit >= base
            || value > (std::numeric_limits<std::uint32_t>::max()
                            - static_cast<std::uint32_t>(digit))
                    / static_cast<std::uint32_t>(base)) {
            throw FastXlsxError(
                "worksheet image reader contains an invalid XML entity in "
                + std::string(label));
        }
        value = value * static_cast<std::uint32_t>(base)
            + static_cast<std::uint32_t>(digit);
    }
    return value;
}

std::string decode_xml_value(
    std::string_view raw, std::size_t limit, std::string_view label)
{
    std::string output;
    output.reserve(std::min(raw.size(), limit));
    std::size_t position = 0;
    while (position < raw.size()) {
        if (raw[position] != '&') {
            const unsigned char byte = static_cast<unsigned char>(raw[position]);
            if (byte == 0U) {
                throw FastXlsxError(
                    "worksheet image reader contains an invalid XML character in "
                    + std::string(label));
            }
            if (output.size() >= limit) {
                throw FastXlsxError(
                    "worksheet image reader " + std::string(label)
                    + " exceeds its configured text limit");
            }
            output.push_back(is_xml_space(raw[position]) ? ' ' : raw[position]);
            ++position;
            continue;
        }
        const std::size_t semicolon = raw.find(';', position + 1U);
        if (semicolon == std::string_view::npos) {
            throw FastXlsxError(
                "worksheet image reader contains an unterminated XML entity in "
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
    return output;
}

std::uint64_t parse_unsigned_decimal(
    std::string_view value, std::string_view label)
{
    if (value.empty()) {
        throw FastXlsxError(
            "worksheet image reader has an invalid " + std::string(label));
    }
    std::uint64_t result = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            throw FastXlsxError(
                "worksheet image reader has an invalid " + std::string(label));
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            throw FastXlsxError(
                "worksheet image reader has an invalid " + std::string(label));
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
                    "worksheet image reader contains an invalid namespace declaration");
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
                "worksheet image reader has an unbound XML namespace prefix on "
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
    Drawing,
};

struct WorksheetFrame {
    std::string qualified_name;
    WorksheetFrameRole role = WorksheetFrameRole::Generic;
    std::vector<NamespaceChange> namespace_changes;
};

class WorksheetDrawingReferenceReader {
public:
    WorksheetDrawingReferenceReader(
        WorksheetImageReaderOptions options, WorksheetImageReadSummary& summary)
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
                    "worksheet drawing reference contains unsupported non-element content");
            }
            return;
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::ProcessingInstruction:
            if (inside_target()) {
                throw FastXlsxError(
                    "worksheet drawing reference contains unsupported nested markup");
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
                "worksheet image reader requires a worksheet root and closed sheetData");
        }
        if (!stack_.empty()) {
            throw FastXlsxError(
                "worksheet image reader ended inside an open worksheet element");
        }
        return std::move(drawing_relationship_id_);
    }

private:
    void consume_worksheet_start(const WorksheetEvent& event)
    {
        if (saw_worksheet_start_) {
            throw FastXlsxError("worksheet image reader found duplicate worksheet roots");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.self_closing || tag.local_name != "worksheet") {
            throw FastXlsxError("worksheet image reader found an invalid worksheet root");
        }
        root_namespace_changes_ = namespaces_.apply(tag, options_.max_xml_window_bytes);
        const QualifiedName name {tag.prefix, tag.local_name};
        if (namespaces_.resolve_element(name) != spreadsheet_namespace) {
            throw FastXlsxError(
                "worksheet image reader requires the spreadsheet namespace on worksheet");
        }
        root_qualified_name_ = std::string(tag.qualified_name);
        saw_worksheet_start_ = true;
    }

    void consume_worksheet_end(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.self_closing || !tag.closing || tag.qualified_name != root_qualified_name_
            || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet image reader worksheet root QName is mismatched");
        }
        saw_worksheet_end_ = true;
        namespaces_.restore(root_namespace_changes_);
    }

    void consume_sheet_data_start(const WorksheetEvent& event)
    {
        if (!saw_worksheet_start_ || saw_sheet_data_start_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet image reader found an invalid sheetData boundary");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "sheetData") {
            throw FastXlsxError(
                "worksheet image reader found an invalid sheetData start");
        }
        if (std::any_of(tag.attributes.begin(), tag.attributes.end(),
                [](const auto& attribute) { return is_namespace_declaration(attribute.first); })) {
            throw FastXlsxError(
                "worksheet image reader does not support namespace declarations on sheetData");
        }
        const QualifiedName name {tag.prefix, tag.local_name};
        if (namespaces_.resolve_element(name) != spreadsheet_namespace) {
            throw FastXlsxError(
                "worksheet image reader sheetData is not in the spreadsheet namespace");
        }
        saw_sheet_data_start_ = true;
    }

    void consume_sheet_data_end(const WorksheetEvent& event)
    {
        if (!saw_sheet_data_start_ || saw_sheet_data_end_) {
            throw FastXlsxError(
                "worksheet image reader found a duplicate sheetData end");
        }
        if (!event.self_closing) {
            const ParsedTag tag = parse_tag(event.raw_xml);
            const QualifiedName name {tag.prefix, tag.local_name};
            if (!tag.closing || tag.local_name != "sheetData"
                || namespaces_.resolve_element(name) != spreadsheet_namespace) {
                throw FastXlsxError(
                    "worksheet image reader sheetData QName is mismatched");
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
            throw FastXlsxError("worksheet drawing reference contains unexpected text");
        }
        if (stack_.empty()) {
            throw FastXlsxError("worksheet image reader found unexpected worksheet text");
        }
    }

    [[nodiscard]] bool inside_target() const noexcept
    {
        return std::any_of(stack_.begin(), stack_.end(), [](const WorksheetFrame& frame) {
            return frame.role == WorksheetFrameRole::Drawing;
        });
    }

    void consume_metadata(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.local_name != event.element_name) {
            throw FastXlsxError(
                "worksheet image reader element local name is mismatched");
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
                "worksheet image reader contains mismatched element QName nesting");
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
                "worksheet image reader found a foreign top-level worksheet element");
        }
        if (!saw_sheet_data_start_) {
            const std::optional<int> rank = prefix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet image reader found unsupported metadata before sheetData");
            }
            if (*rank < last_prefix_rank_) {
                throw FastXlsxError(
                    "worksheet image reader prefix elements are not in schema order");
            }
            last_prefix_rank_ = *rank;
            return;
        }
        if (!saw_sheet_data_end_) {
            throw FastXlsxError("worksheet drawing appears inside sheetData");
        }
        const std::optional<int> rank = suffix_schema_rank(tag.local_name);
        if (!rank.has_value()) {
            throw FastXlsxError(
                "worksheet image reader found unsupported worksheet suffix metadata");
        }
        if (*rank < last_suffix_rank_) {
            throw FastXlsxError(
                "worksheet image reader suffix elements are not in schema order");
        }
        last_suffix_rank_ = *rank;
    }

    void open_metadata(const ParsedTag& tag)
    {
        std::vector<NamespaceChange> namespace_changes =
            namespaces_.apply(tag, options_.max_xml_window_bytes);
        const QualifiedName name {tag.prefix, tag.local_name};
        const std::string_view namespace_uri = namespaces_.resolve_element(name);
        enforce_top_level_schema(tag, namespace_uri);
        if (namespace_uri == spreadsheet_namespace && tag.local_name == "drawing") {
            open_drawing(tag, std::move(namespace_changes));
            return;
        }
        if (inside_target()) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError(
                "worksheet drawing reference contains an unsupported child element");
        }
        open_generic(tag, std::move(namespace_changes));
    }

    void open_drawing(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (!stack_.empty() || drawing_relationship_id_.has_value()) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError(
                "worksheet image reader found duplicate or nested drawing references");
        }
        std::optional<std::string> relationship_id;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name =
                parse_qualified_name(name, "drawing attribute name");
            if (attribute_name.local_name != "id"
                || namespaces_.resolve_attribute(attribute_name) != relationship_namespace) {
                namespaces_.restore(namespace_changes);
                throw FastXlsxError(
                    "worksheet drawing requires an OpenXML relationship id");
            }
            if (relationship_id.has_value()) {
                namespaces_.restore(namespace_changes);
                throw FastXlsxError(
                    "worksheet drawing contains duplicate semantic relationship ids");
            }
            relationship_id = decode_xml_value(raw_value,
                options_.max_relationship_id_bytes,
                "relationship id (max_relationship_id_bytes)");
        }
        if (!relationship_id.has_value() || relationship_id->empty()) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError("worksheet drawing relationship id cannot be empty");
        }
        summary_.peak_relationship_id_bytes = std::max(
            summary_.peak_relationship_id_bytes, relationship_id->size());
        drawing_relationship_id_ = std::move(*relationship_id);
        if (tag.self_closing) {
            namespaces_.restore(namespace_changes);
            return;
        }
        push_frame(tag, WorksheetFrameRole::Drawing, std::move(namespace_changes));
    }

    void open_generic(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (tag.self_closing) {
            namespaces_.restore(namespace_changes);
            return;
        }
        push_frame(tag, WorksheetFrameRole::Generic, std::move(namespace_changes));
    }

    void push_frame(const ParsedTag& tag, WorksheetFrameRole role,
        std::vector<NamespaceChange> namespace_changes)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError(
                "worksheet image reader exceeds max_xml_nesting_depth");
        }
        stack_.push_back(WorksheetFrame {std::string(tag.qualified_name), role,
            std::move(namespace_changes)});
        summary_.peak_xml_nesting_depth = std::max(
            summary_.peak_xml_nesting_depth, stack_.size());
    }

    WorksheetImageReaderOptions options_;
    WorksheetImageReadSummary& summary_;
    NamespaceBindings namespaces_;
    std::vector<WorksheetFrame> stack_;
    std::vector<NamespaceChange> root_namespace_changes_;
    std::optional<std::string> drawing_relationship_id_;
    std::string root_qualified_name_;
    int last_prefix_rank_ = 0;
    int last_suffix_rank_ = 0;
    bool saw_worksheet_start_ = false;
    bool saw_sheet_data_start_ = false;
    bool saw_sheet_data_end_ = false;
    bool saw_worksheet_end_ = false;
    bool inside_cell_ = false;
};

enum class DrawingRole {
    Root,
    Anchor,
    From,
    To,
    Col,
    ColOffset,
    Row,
    RowOffset,
    Picture,
    NonVisualPictureProperties,
    NonVisualProperties,
    NonVisualPicturePropertiesExtension,
    PictureLocks,
    BlipFill,
    Blip,
    Stretch,
    FillRect,
    ShapeProperties,
    Transform,
    TransformOffset,
    TransformExtent,
    PresetGeometry,
    AdjustmentList,
    ClientData,
};

struct DrawingFrame {
    std::string qualified_name;
    DrawingRole role = DrawingRole::Root;
    std::size_t child_count = 0;
    std::vector<NamespaceChange> namespace_changes;
};

[[nodiscard]] bool is_numeric_role(DrawingRole role) noexcept
{
    return role == DrawingRole::Col || role == DrawingRole::ColOffset
        || role == DrawingRole::Row || role == DrawingRole::RowOffset;
}

[[nodiscard]] bool is_leaf_role(DrawingRole role) noexcept
{
    return role == DrawingRole::NonVisualProperties
        || role == DrawingRole::PictureLocks || role == DrawingRole::Blip
        || role == DrawingRole::FillRect || role == DrawingRole::TransformOffset
        || role == DrawingRole::TransformExtent || role == DrawingRole::AdjustmentList
        || role == DrawingRole::ClientData;
}

class DrawingProjectionReader {
public:
    using EmitCallback = std::function<void(WorksheetImageView, std::string)>;

    DrawingProjectionReader(WorksheetImageReaderOptions options,
        WorksheetImageReadSummary& summary, EmitCallback emit)
        : options_(options)
        , summary_(summary)
        , emit_(std::move(emit))
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
                throw FastXlsxError("worksheet drawing contains unexpected element text");
            }
            return;
        }
        if (!is_numeric_role(stack_.back().role)) {
            if (has_non_whitespace(text)) {
                throw FastXlsxError("worksheet drawing contains unexpected element text");
            }
            return;
        }
        if (numeric_text_.size() > options_.max_numeric_text_bytes
            || text.size() > options_.max_numeric_text_bytes - numeric_text_.size()) {
            throw FastXlsxError(
                "worksheet drawing numeric text exceeds max_numeric_text_bytes");
        }
        numeric_text_.append(text);
        summary_.peak_numeric_text_bytes = std::max(
            summary_.peak_numeric_text_bytes, numeric_text_.size());
    }

    void consume_special_markup() const
    {
        if (saw_root_ && !finished_root_) {
            throw FastXlsxError(
                "worksheet drawing contains unsupported nested markup");
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
            throw FastXlsxError("worksheet image reader requires one closed drawing root");
        }
    }

private:
    [[nodiscard]] std::string_view resolved_element_uri(const ParsedTag& tag) const
    {
        return namespaces_.resolve_element(QualifiedName {tag.prefix, tag.local_name});
    }

    void open_tag(const ParsedTag& tag)
    {
        if (!saw_root_) {
            open_root(tag);
            return;
        }
        if (finished_root_ || stack_.empty()) {
            throw FastXlsxError("worksheet drawing contains multiple root elements");
        }
        if (std::any_of(tag.attributes.begin(), tag.attributes.end(),
                [](const auto& attribute) { return is_namespace_declaration(attribute.first); })) {
            throw FastXlsxError(
                "worksheet drawing does not support nested namespace declarations");
        }
        const std::vector<NamespaceChange> namespace_changes =
            namespaces_.apply(tag, options_.max_xml_window_bytes);
        const std::string_view namespace_uri = resolved_element_uri(tag);
        DrawingFrame& parent = stack_.back();
        const DrawingRole role = child_role(parent, namespace_uri, tag.local_name);
        ++parent.child_count;
        begin_role(role, tag);

        if (is_leaf_role(role)) {
            if (!tag.self_closing) {
                throw FastXlsxError(
                    role == DrawingRole::NonVisualProperties
                        ? "worksheet drawing does not support picture hyperlinks or nested cNvPr content"
                        : "worksheet drawing requires a self-closing leaf element");
            }
            namespaces_.restore(namespace_changes);
            return;
        }
        if (tag.self_closing) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError("worksheet drawing contains a self-closing structural element");
        }
        push_frame(tag, role, namespace_changes);
    }

    void open_root(const ParsedTag& tag)
    {
        if (tag.closing || tag.local_name != "wsDr") {
            throw FastXlsxError("worksheet image reader requires an xdr:wsDr drawing root");
        }
        const std::vector<NamespaceChange> namespace_changes =
            namespaces_.apply(tag, options_.max_xml_window_bytes);
        for (const auto& [name, _] : tag.attributes) {
            if (!is_namespace_declaration(name)) {
                namespaces_.restore(namespace_changes);
                throw FastXlsxError("worksheet drawing root has an unsupported attribute");
            }
        }
        if (resolved_element_uri(tag) != drawing_namespace) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError("worksheet drawing root is not in the xdr namespace");
        }
        saw_root_ = true;
        if (tag.self_closing) {
            namespaces_.restore(namespace_changes);
            finished_root_ = true;
            return;
        }
        push_frame(tag, DrawingRole::Root, namespace_changes);
    }

    void close_tag(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().qualified_name != tag.qualified_name) {
            throw FastXlsxError(
                "worksheet drawing contains mismatched element QName nesting");
        }
        const DrawingRole role = stack_.back().role;
        validate_close(role, stack_.back().child_count);
        if (is_numeric_role(role)) {
            assign_numeric(role);
        }
        if (role == DrawingRole::Anchor) {
            emit_anchor();
        }
        DrawingFrame frame = std::move(stack_.back());
        stack_.pop_back();
        namespaces_.restore(frame.namespace_changes);
        if (role == DrawingRole::Root) {
            finished_root_ = true;
        }
    }

    [[nodiscard]] DrawingRole child_role(const DrawingFrame& parent,
        std::string_view namespace_uri, std::string_view local_name) const
    {
        const auto require = [&](std::size_t expected_index,
                                 std::string_view expected_namespace,
                                 std::string_view expected_name,
                                 DrawingRole role) {
            if (parent.child_count != expected_index
                || namespace_uri != expected_namespace || local_name != expected_name) {
                throw FastXlsxError("worksheet drawing contains an unsupported child order or QName");
            }
            return role;
        };

        switch (parent.role) {
        case DrawingRole::Root:
            if (namespace_uri == drawing_namespace && local_name == "twoCellAnchor") {
                return DrawingRole::Anchor;
            }
            if (namespace_uri == drawing_namespace
                && (local_name == "oneCellAnchor" || local_name == "absoluteAnchor")) {
                throw FastXlsxError(
                    "worksheet image reader supports only xdr:twoCellAnchor");
            }
            throw FastXlsxError(
                "worksheet image reader does not support non-picture drawing objects");
        case DrawingRole::Anchor:
            return parent.child_count == 0
                ? require(0, drawing_namespace, "from", DrawingRole::From)
                : (parent.child_count == 1
                        ? require(1, drawing_namespace, "to", DrawingRole::To)
                        : (parent.child_count == 2
                                ? require(2, drawing_namespace, "pic", DrawingRole::Picture)
                                : require(3, drawing_namespace, "clientData", DrawingRole::ClientData)));
        case DrawingRole::From:
        case DrawingRole::To:
            return parent.child_count == 0
                ? require(0, drawing_namespace, "col", DrawingRole::Col)
                : (parent.child_count == 1
                        ? require(1, drawing_namespace, "colOff", DrawingRole::ColOffset)
                        : (parent.child_count == 2
                                ? require(2, drawing_namespace, "row", DrawingRole::Row)
                                : require(3, drawing_namespace, "rowOff", DrawingRole::RowOffset)));
        case DrawingRole::Picture:
            return parent.child_count == 0
                ? require(0, drawing_namespace, "nvPicPr", DrawingRole::NonVisualPictureProperties)
                : (parent.child_count == 1
                        ? require(1, drawing_namespace, "blipFill", DrawingRole::BlipFill)
                        : require(2, drawing_namespace, "spPr", DrawingRole::ShapeProperties));
        case DrawingRole::NonVisualPictureProperties:
            return parent.child_count == 0
                ? require(0, drawing_namespace, "cNvPr", DrawingRole::NonVisualProperties)
                : require(1, drawing_namespace, "cNvPicPr",
                    DrawingRole::NonVisualPicturePropertiesExtension);
        case DrawingRole::NonVisualPicturePropertiesExtension:
            return require(0, drawing_main_namespace, "picLocks", DrawingRole::PictureLocks);
        case DrawingRole::BlipFill:
            return parent.child_count == 0
                ? require(0, drawing_main_namespace, "blip", DrawingRole::Blip)
                : require(1, drawing_main_namespace, "stretch", DrawingRole::Stretch);
        case DrawingRole::Stretch:
            return require(0, drawing_main_namespace, "fillRect", DrawingRole::FillRect);
        case DrawingRole::ShapeProperties:
            return parent.child_count == 0
                ? require(0, drawing_main_namespace, "xfrm", DrawingRole::Transform)
                : require(1, drawing_main_namespace, "prstGeom", DrawingRole::PresetGeometry);
        case DrawingRole::Transform:
            return parent.child_count == 0
                ? require(0, drawing_main_namespace, "off", DrawingRole::TransformOffset)
                : require(1, drawing_main_namespace, "ext", DrawingRole::TransformExtent);
        case DrawingRole::PresetGeometry:
            return require(0, drawing_main_namespace, "avLst", DrawingRole::AdjustmentList);
        case DrawingRole::Col:
        case DrawingRole::ColOffset:
        case DrawingRole::Row:
        case DrawingRole::RowOffset:
        case DrawingRole::NonVisualProperties:
        case DrawingRole::PictureLocks:
        case DrawingRole::Blip:
        case DrawingRole::FillRect:
        case DrawingRole::TransformOffset:
        case DrawingRole::TransformExtent:
        case DrawingRole::AdjustmentList:
        case DrawingRole::ClientData:
            break;
        }
        throw FastXlsxError("worksheet drawing contains an unsupported nested child");
    }

    void begin_role(DrawingRole role, const ParsedTag& tag)
    {
        switch (role) {
        case DrawingRole::Anchor:
            begin_anchor(tag);
            return;
        case DrawingRole::Col:
        case DrawingRole::ColOffset:
        case DrawingRole::Row:
        case DrawingRole::RowOffset:
            require_no_attributes(tag);
            numeric_text_.clear();
            return;
        case DrawingRole::NonVisualProperties:
            begin_non_visual_properties(tag);
            return;
        case DrawingRole::PictureLocks:
            begin_picture_locks(tag);
            return;
        case DrawingRole::Blip:
            begin_blip(tag);
            return;
        case DrawingRole::TransformOffset:
            begin_transform_offset(tag);
            return;
        case DrawingRole::TransformExtent:
            begin_transform_extent(tag);
            return;
        case DrawingRole::PresetGeometry:
            begin_preset_geometry(tag);
            return;
        case DrawingRole::Root:
        case DrawingRole::From:
        case DrawingRole::To:
        case DrawingRole::Picture:
        case DrawingRole::NonVisualPictureProperties:
        case DrawingRole::NonVisualPicturePropertiesExtension:
        case DrawingRole::BlipFill:
        case DrawingRole::Stretch:
        case DrawingRole::FillRect:
        case DrawingRole::ShapeProperties:
        case DrawingRole::Transform:
        case DrawingRole::AdjustmentList:
        case DrawingRole::ClientData:
            require_no_attributes(tag);
            return;
        }
    }

    void require_no_attributes(const ParsedTag& tag) const
    {
        for (const auto& [name, _] : tag.attributes) {
            if (!is_namespace_declaration(name)) {
                throw FastXlsxError("worksheet drawing has an unsupported attribute");
            }
        }
    }

    [[nodiscard]] std::uint64_t parse_numeric_attribute(
        std::string_view raw_value, std::string_view label, std::uint64_t maximum)
    {
        if (raw_value.size() > options_.max_numeric_text_bytes) {
            throw FastXlsxError(
                "worksheet drawing numeric text exceeds max_numeric_text_bytes");
        }
        summary_.peak_numeric_text_bytes = std::max(
            summary_.peak_numeric_text_bytes, raw_value.size());
        const std::uint64_t value = parse_unsigned_decimal(raw_value, label);
        if (value > maximum) {
            throw FastXlsxError(
                "worksheet drawing " + std::string(label) + " exceeds supported bounds");
        }
        return value;
    }

    void begin_anchor(const ParsedTag& tag)
    {
        if (anchor_count_ >= options_.max_image_count) {
            throw FastXlsxError("worksheet drawing exceeds max_image_count");
        }
        std::optional<ImageEditAs> edit_as;
        for (const auto& [name, raw_value] : tag.attributes) {
            const QualifiedName attribute_name =
                parse_qualified_name(name, "twoCellAnchor attribute name");
            if (!attribute_name.prefix.empty() || attribute_name.local_name != "editAs"
                || edit_as.has_value()) {
                throw FastXlsxError("worksheet drawing twoCellAnchor has an unsupported attribute");
            }
            const std::string value = decode_xml_value(raw_value, 16U, "twoCellAnchor editAs");
            if (value == "twoCell") {
                edit_as = ImageEditAs::TwoCell;
            } else if (value == "oneCell") {
                edit_as = ImageEditAs::OneCell;
            } else if (value == "absolute") {
                edit_as = ImageEditAs::Absolute;
            } else {
                throw FastXlsxError("worksheet drawing has an unsupported twoCellAnchor editAs");
            }
        }
        if (!edit_as.has_value()) {
            throw FastXlsxError("worksheet drawing twoCellAnchor requires editAs");
        }
        current_view_ = WorksheetImageView {};
        current_view_.edit_as = *edit_as;
        embed_relationship_id_.reset();
        ++anchor_count_;
    }

    void begin_non_visual_properties(const ParsedTag& tag)
    {
        std::optional<std::uint64_t> object_id;
        std::optional<std::string> name;
        std::optional<std::string> description;
        for (const auto& [attribute, raw_value] : tag.attributes) {
            const QualifiedName attribute_name =
                parse_qualified_name(attribute, "cNvPr attribute name");
            if (!attribute_name.prefix.empty()) {
                throw FastXlsxError("worksheet drawing cNvPr has an unsupported attribute");
            }
            if (attribute_name.local_name == "id" && !object_id.has_value()) {
                const std::uint64_t value = parse_numeric_attribute(
                    raw_value, "cNvPr id", std::numeric_limits<std::uint32_t>::max());
                if (value == 0 || !picture_ids_.insert(value).second) {
                    throw FastXlsxError("worksheet drawing cNvPr id must be unique and nonzero");
                }
                object_id = value;
            } else if (attribute_name.local_name == "name" && !name.has_value()) {
                name = decode_xml_value(raw_value, options_.max_name_bytes,
                    "name (max_name_bytes)");
            } else if (attribute_name.local_name == "descr" && !description.has_value()) {
                description = decode_xml_value(raw_value, options_.max_description_bytes,
                    "description (max_description_bytes)");
            } else {
                throw FastXlsxError("worksheet drawing cNvPr has an unsupported attribute");
            }
        }
        if (!object_id.has_value() || !name.has_value() || name->empty()) {
            throw FastXlsxError("worksheet drawing cNvPr requires non-empty id and name");
        }
        current_view_.name = std::move(*name);
        current_view_.description = description.value_or(std::string {});
        summary_.peak_name_bytes = std::max(summary_.peak_name_bytes,
            current_view_.name.size());
        summary_.peak_description_bytes = std::max(summary_.peak_description_bytes,
            current_view_.description.size());
    }

    void begin_picture_locks(const ParsedTag& tag) const
    {
        bool saw_no_change_aspect = false;
        for (const auto& [attribute, raw_value] : tag.attributes) {
            const QualifiedName attribute_name =
                parse_qualified_name(attribute, "picLocks attribute name");
            if (!attribute_name.prefix.empty()
                || attribute_name.local_name != "noChangeAspect"
                || saw_no_change_aspect
                || (raw_value != "1" && raw_value != "true")) {
                throw FastXlsxError("worksheet drawing picLocks is outside the narrow projection");
            }
            saw_no_change_aspect = true;
        }
        if (!saw_no_change_aspect) {
            throw FastXlsxError("worksheet drawing picLocks requires noChangeAspect");
        }
    }

    void begin_blip(const ParsedTag& tag)
    {
        std::optional<std::string> relationship_id;
        for (const auto& [attribute, raw_value] : tag.attributes) {
            const QualifiedName attribute_name =
                parse_qualified_name(attribute, "blip attribute name");
            if (attribute_name.local_name != "embed"
                || namespaces_.resolve_attribute(attribute_name) != relationship_namespace
                || relationship_id.has_value()) {
                throw FastXlsxError("worksheet drawing blip requires an OpenXML embedded relationship");
            }
            relationship_id = decode_xml_value(raw_value,
                options_.max_relationship_id_bytes,
                "relationship id (max_relationship_id_bytes)");
        }
        if (!relationship_id.has_value() || relationship_id->empty()) {
            throw FastXlsxError("worksheet drawing blip relationship id cannot be empty");
        }
        summary_.peak_relationship_id_bytes = std::max(
            summary_.peak_relationship_id_bytes, relationship_id->size());
        embed_relationship_id_ = std::move(*relationship_id);
    }

    void begin_transform_offset(const ParsedTag& tag)
    {
        bool saw_x = false;
        bool saw_y = false;
        for (const auto& [attribute, raw_value] : tag.attributes) {
            const QualifiedName attribute_name =
                parse_qualified_name(attribute, "xfrm off attribute name");
            const std::uint64_t value = parse_numeric_attribute(
                raw_value, "xfrm off coordinate", max_openxml_coordinate);
            if (!attribute_name.prefix.empty() || value != 0
                || (attribute_name.local_name == "x" ? saw_x :
                    (attribute_name.local_name == "y" ? saw_y : true))) {
                throw FastXlsxError(
                    "worksheet image reader does not support transformed picture position");
            }
            if (attribute_name.local_name == "x") {
                saw_x = true;
            } else {
                saw_y = true;
            }
        }
        if (!saw_x || !saw_y) {
            throw FastXlsxError("worksheet drawing xfrm off requires x and y");
        }
    }

    void begin_transform_extent(const ParsedTag& tag)
    {
        std::optional<std::uint64_t> width;
        std::optional<std::uint64_t> height;
        for (const auto& [attribute, raw_value] : tag.attributes) {
            const QualifiedName attribute_name =
                parse_qualified_name(attribute, "xfrm ext attribute name");
            const std::uint64_t value = parse_numeric_attribute(
                raw_value, "xfrm extent", max_openxml_coordinate);
            if (!attribute_name.prefix.empty()) {
                throw FastXlsxError("worksheet drawing xfrm ext has an unsupported attribute");
            }
            if (attribute_name.local_name == "cx" && !width.has_value()) {
                width = value;
            } else if (attribute_name.local_name == "cy" && !height.has_value()) {
                height = value;
            } else {
                throw FastXlsxError("worksheet drawing xfrm ext has an unsupported attribute");
            }
        }
        if (!width.has_value() || !height.has_value() || *width == 0 || *height == 0) {
            throw FastXlsxError("worksheet drawing xfrm ext requires positive cx and cy");
        }
        current_view_.transform_width_emu = *width;
        current_view_.transform_height_emu = *height;
    }

    void begin_preset_geometry(const ParsedTag& tag) const
    {
        bool saw_preset = false;
        for (const auto& [attribute, raw_value] : tag.attributes) {
            const QualifiedName attribute_name =
                parse_qualified_name(attribute, "prstGeom attribute name");
            if (!attribute_name.prefix.empty() || attribute_name.local_name != "prst"
                || saw_preset || raw_value != "rect") {
                throw FastXlsxError("worksheet drawing prstGeom is outside the narrow projection");
            }
            saw_preset = true;
        }
        if (!saw_preset) {
            throw FastXlsxError("worksheet drawing prstGeom requires rect preset");
        }
    }

    void validate_close(DrawingRole role, std::size_t child_count) const
    {
        const auto require_count = [role, child_count](std::size_t expected) {
            if (child_count != expected) {
                throw FastXlsxError("worksheet drawing has an incomplete or unsupported element shape");
            }
        };
        switch (role) {
        case DrawingRole::Root:
            return;
        case DrawingRole::Anchor:
            require_count(4);
            return;
        case DrawingRole::From:
        case DrawingRole::To:
            require_count(4);
            return;
        case DrawingRole::Picture:
            require_count(3);
            return;
        case DrawingRole::NonVisualPictureProperties:
            require_count(2);
            return;
        case DrawingRole::NonVisualPicturePropertiesExtension:
        case DrawingRole::Stretch:
        case DrawingRole::PresetGeometry:
            require_count(1);
            return;
        case DrawingRole::BlipFill:
        case DrawingRole::ShapeProperties:
        case DrawingRole::Transform:
            require_count(2);
            return;
        case DrawingRole::Col:
        case DrawingRole::ColOffset:
        case DrawingRole::Row:
        case DrawingRole::RowOffset:
        case DrawingRole::NonVisualProperties:
        case DrawingRole::PictureLocks:
        case DrawingRole::Blip:
        case DrawingRole::FillRect:
        case DrawingRole::TransformOffset:
        case DrawingRole::TransformExtent:
        case DrawingRole::AdjustmentList:
        case DrawingRole::ClientData:
            require_count(0);
            return;
        }
    }

    void assign_numeric(DrawingRole role)
    {
        if (stack_.size() < 2) {
            throw FastXlsxError("worksheet drawing numeric marker is outside an anchor");
        }
        const DrawingRole marker_role = stack_[stack_.size() - 2U].role;
        if (marker_role != DrawingRole::From && marker_role != DrawingRole::To) {
            throw FastXlsxError("worksheet drawing numeric marker has an invalid parent");
        }
        const std::string_view text = trim(numeric_text_);
        const std::uint64_t value = parse_numeric_attribute(text, "anchor marker", max_openxml_coordinate);
        WorksheetImageAnchorMarker& marker = marker_role == DrawingRole::From
            ? current_view_.from
            : current_view_.to;
        switch (role) {
        case DrawingRole::Col:
            if (value > max_excel_columns
                || (marker_role == DrawingRole::From && value >= max_excel_columns)) {
                throw FastXlsxError("worksheet drawing anchor column is outside Excel bounds");
            }
            marker.column_index = static_cast<std::uint32_t>(value);
            return;
        case DrawingRole::Row:
            if (value > max_excel_rows
                || (marker_role == DrawingRole::From && value >= max_excel_rows)) {
                throw FastXlsxError("worksheet drawing anchor row is outside Excel bounds");
            }
            marker.row_index = static_cast<std::uint32_t>(value);
            return;
        case DrawingRole::ColOffset:
            marker.offset.column_emu = static_cast<std::int64_t>(value);
            return;
        case DrawingRole::RowOffset:
            marker.offset.row_emu = static_cast<std::int64_t>(value);
            return;
        default:
            break;
        }
        throw FastXlsxError("worksheet drawing has an invalid numeric marker");
    }

    void emit_anchor()
    {
        if (!embed_relationship_id_.has_value()
            || current_view_.name.empty()
            || current_view_.transform_width_emu == 0
            || current_view_.transform_height_emu == 0) {
            throw FastXlsxError("worksheet drawing picture is incomplete");
        }
        if (current_view_.from.column_index >= current_view_.to.column_index
            || current_view_.from.row_index >= current_view_.to.row_index) {
            throw FastXlsxError(
                "worksheet drawing twoCellAnchor markers must define a non-empty range");
        }
        emit_(std::move(current_view_), std::move(*embed_relationship_id_));
        embed_relationship_id_.reset();
    }

    void push_frame(const ParsedTag& tag, DrawingRole role,
        const std::vector<NamespaceChange>& namespace_changes)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            namespaces_.restore(namespace_changes);
            throw FastXlsxError("worksheet drawing exceeds max_xml_nesting_depth");
        }
        stack_.push_back(DrawingFrame {std::string(tag.qualified_name), role, 0,
            namespace_changes});
        summary_.peak_xml_nesting_depth = std::max(
            summary_.peak_xml_nesting_depth, stack_.size());
    }

    WorksheetImageReaderOptions options_;
    WorksheetImageReadSummary& summary_;
    EmitCallback emit_;
    NamespaceBindings namespaces_;
    std::vector<DrawingFrame> stack_;
    std::set<std::uint64_t> picture_ids_;
    WorksheetImageView current_view_;
    std::optional<std::string> embed_relationship_id_;
    std::string numeric_text_;
    std::size_t anchor_count_ = 0;
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
    std::string_view target, const WorksheetImageReaderOptions& options,
    WorksheetImageReadSummary& summary)
{
    if (target.empty()) {
        throw FastXlsxError("worksheet image relationship target cannot be empty");
    }
    if (target.size() > options.max_relationship_target_bytes) {
        throw FastXlsxError(
            "worksheet image relationship target exceeds max_relationship_target_bytes");
    }
    std::string decoded;
    decoded.reserve(target.size());
    for (std::size_t index = 0; index < target.size(); ++index) {
        if (target[index] != '%') {
            if (target[index] == '\0') {
                throw FastXlsxError(
                    "worksheet image relationship target contains a null byte");
            }
            decoded.push_back(target[index]);
            continue;
        }
        if (index + 2U >= target.size()) {
            throw FastXlsxError(
                "worksheet image relationship target has invalid percent encoding");
        }
        const int high = hex_digit_value(target[index + 1U]);
        const int low = hex_digit_value(target[index + 2U]);
        if (high < 0 || low < 0) {
            throw FastXlsxError(
                "worksheet image relationship target has invalid percent encoding");
        }
        const char decoded_byte = static_cast<char>((high << 4) | low);
        if (decoded_byte == '\0') {
            throw FastXlsxError(
                "worksheet image relationship target decodes to a null byte");
        }
        decoded.push_back(decoded_byte);
        index += 2U;
    }
    if (decoded.size() > options.max_relationship_target_bytes) {
        throw FastXlsxError(
            "worksheet image relationship target exceeds max_relationship_target_bytes");
    }
    summary.peak_relationship_target_bytes = std::max(
        summary.peak_relationship_target_bytes, decoded.size());
    return decoded;
}

PartName resolve_relationship_part(const PartName& owner_part,
    const Relationship& relationship, const WorksheetImageReaderOptions& options,
    WorksheetImageReadSummary& summary, std::string_view relationship_name)
{
    std::string target = decode_relationship_target(relationship.target, options, summary);
    if (target.find_first_of("?#") != std::string::npos) {
        throw FastXlsxError("worksheet image " + std::string(relationship_name)
            + " relationship target cannot contain a query or fragment");
    }
    if (!target.empty() && target.front() == '/') {
        return PartName(target);
    }
    const std::string source = owner_part.value();
    const std::size_t slash = source.find_last_of('/');
    if (slash == std::string::npos) {
        throw FastXlsxError("worksheet image relationship owner has no package directory");
    }
    return PartName(source.substr(0, slash) + "/" + target);
}

void validate_options(const WorksheetImageReaderOptions& options)
{
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError("WorksheetImageReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError("WorksheetImageReader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_image_count == 0) {
        throw FastXlsxError("WorksheetImageReader requires nonzero max_image_count");
    }
    if (options.max_relationship_id_bytes == 0) {
        throw FastXlsxError(
            "WorksheetImageReader requires nonzero max_relationship_id_bytes");
    }
    if (options.max_relationship_target_bytes == 0) {
        throw FastXlsxError(
            "WorksheetImageReader requires nonzero max_relationship_target_bytes");
    }
    if (options.max_name_bytes == 0) {
        throw FastXlsxError("WorksheetImageReader requires nonzero max_name_bytes");
    }
    if (options.max_description_bytes == 0) {
        throw FastXlsxError(
            "WorksheetImageReader requires nonzero max_description_bytes");
    }
    if (options.max_numeric_text_bytes == 0) {
        throw FastXlsxError(
            "WorksheetImageReader requires nonzero max_numeric_text_bytes");
    }
    if (options.max_media_bytes == 0) {
        throw FastXlsxError("WorksheetImageReader requires nonzero max_media_bytes");
    }
}

struct MediaProjection {
    ImageFormat format = ImageFormat::Png;
    std::uint64_t encoded_size_bytes = 0;
};

[[nodiscard]] ImageFormat image_format_for_content_type(std::string_view content_type)
{
    if (content_type == png_content_type) {
        return ImageFormat::Png;
    }
    if (content_type == jpeg_content_type) {
        return ImageFormat::Jpeg;
    }
    throw FastXlsxError("worksheet image media target has an unsupported content type");
}

MediaProjection inspect_media_part(const PackageReader& package,
    const PartName& media_part, const WorksheetImageReaderOptions& options,
    WorksheetImageReadSummary& summary)
{
    const PackagePart* indexed_part = package.part_index().find_part(media_part);
    if (indexed_part == nullptr) {
        throw FastXlsxError("worksheet image relationship targets an unknown media part");
    }
    const ImageFormat format = image_format_for_content_type(indexed_part->content_type);
    const PackageReaderEntry* entry = package.find_entry(media_part.zip_path());
    if (entry == nullptr) {
        throw FastXlsxError("worksheet image media part has no ZIP entry");
    }
    if (entry->uncompressed_size > options.max_media_bytes) {
        throw FastXlsxError("worksheet image media entry exceeds max_media_bytes");
    }

    std::array<unsigned char, 8> prefix {};
    std::size_t prefix_size = 0;
    std::uint64_t received_bytes = 0;
    PackageReaderChunkCallback source = package.entry_chunk_source(media_part.zip_path());
    std::string chunk;
    while (source(chunk)) {
        if (chunk.size() > std::numeric_limits<std::uint64_t>::max() - received_bytes) {
            throw FastXlsxError("worksheet image media entry size overflows uint64");
        }
        received_bytes += static_cast<std::uint64_t>(chunk.size());
        if (received_bytes > options.max_media_bytes) {
            throw FastXlsxError("worksheet image media entry exceeds max_media_bytes");
        }
        if (prefix_size < prefix.size()) {
            const std::size_t copy_size =
                std::min(prefix.size() - prefix_size, chunk.size());
            std::copy_n(reinterpret_cast<const unsigned char*>(chunk.data()),
                copy_size, prefix.data() + prefix_size);
            prefix_size += copy_size;
        }
    }
    if (received_bytes != entry->uncompressed_size) {
        throw FastXlsxError("worksheet image media entry size differs from ZIP metadata");
    }
    if (format == ImageFormat::Png) {
        static constexpr std::array<unsigned char, 8> png_signature {
            0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
        if (prefix_size != png_signature.size()
            || !std::equal(prefix.begin(), prefix.end(), png_signature.begin())) {
            throw FastXlsxError("worksheet image PNG media signature is invalid");
        }
    } else if (prefix_size < 3 || prefix[0] != 0xFFU || prefix[1] != 0xD8U
        || prefix[2] != 0xFFU) {
        throw FastXlsxError("worksheet image JPEG media signature is invalid");
    }
    summary.peak_media_bytes = std::max(summary.peak_media_bytes, received_bytes);
    return MediaProjection {format, received_bytes};
}

} // namespace

WorksheetImageReadSummary read_worksheet_images_from_package(
    const PackageReader& package,
    const PartName& worksheet_part,
    const WorksheetImageReadCallbacks& callbacks,
    WorksheetImageReaderOptions options)
{
    validate_options(options);

    WorksheetImageReadSummary summary;
    WorksheetDrawingReferenceReader worksheet_reader(options, summary);
    WorksheetEventReaderOptions event_options;
    event_options.max_window_bytes = options.max_xml_window_bytes;
    event_options.copy_context_attributes = false;
    scan_worksheet_events_from_chunk_source(
        package.entry_chunk_source(worksheet_part.zip_path()),
        [&worksheet_reader](const WorksheetEvent& event) { worksheet_reader.consume(event); },
        event_options);
    const std::optional<std::string> drawing_relationship_id = worksheet_reader.finish();
    if (!drawing_relationship_id.has_value()) {
        return summary;
    }

    const RelationshipSet* worksheet_relationships = package.relationships_for(worksheet_part);
    if (worksheet_relationships == nullptr) {
        throw FastXlsxError("worksheet drawing requires worksheet relationships");
    }
    const Relationship* drawing_relationship =
        worksheet_relationships->find_by_id(*drawing_relationship_id);
    if (drawing_relationship == nullptr) {
        throw FastXlsxError("worksheet drawing relationship id is missing");
    }
    if (drawing_relationship->type != worksheet_drawing_relationship_type) {
        throw FastXlsxError("worksheet drawing relationship has the wrong type");
    }
    if (drawing_relationship->target_mode != Relationship::TargetMode::Internal) {
        throw FastXlsxError("worksheet drawing relationship must be internal");
    }
    const PartName drawing_part = resolve_relationship_part(worksheet_part,
        *drawing_relationship, options, summary, "drawing");
    const PackagePart* indexed_drawing_part = package.part_index().find_part(drawing_part);
    if (indexed_drawing_part == nullptr) {
        throw FastXlsxError("worksheet drawing relationship targets an unknown part");
    }
    if (indexed_drawing_part->content_type != drawing_content_type) {
        throw FastXlsxError("worksheet drawing target has the wrong content type");
    }
    if (package.find_entry(drawing_part.zip_path()) == nullptr) {
        throw FastXlsxError("worksheet drawing part has no ZIP entry");
    }

    const RelationshipSet* drawing_relationships = package.relationships_for(drawing_part);
    std::map<PartName, MediaProjection> media_cache;
    DrawingProjectionReader drawing_reader(options, summary,
        [&](WorksheetImageView view, std::string embedded_relationship_id) {
            if (drawing_relationships == nullptr) {
                throw FastXlsxError("worksheet drawing picture requires drawing relationships");
            }
            const Relationship* media_relationship =
                drawing_relationships->find_by_id(embedded_relationship_id);
            if (media_relationship == nullptr) {
                throw FastXlsxError("worksheet drawing image relationship id is missing");
            }
            if (media_relationship->type != image_relationship_type) {
                throw FastXlsxError("worksheet drawing image relationship has the wrong type");
            }
            if (media_relationship->target_mode != Relationship::TargetMode::Internal) {
                throw FastXlsxError("worksheet drawing image relationship must be internal");
            }
            const PartName media_part = resolve_relationship_part(drawing_part,
                *media_relationship, options, summary, "image");
            auto media = media_cache.find(media_part);
            if (media == media_cache.end()) {
                MediaProjection projection = inspect_media_part(
                    package, media_part, options, summary);
                if (projection.encoded_size_bytes
                    > std::numeric_limits<std::uint64_t>::max() - summary.unique_media_bytes) {
                    throw FastXlsxError("worksheet image unique media bytes overflow uint64");
                }
                summary.unique_media_bytes += projection.encoded_size_bytes;
                ++summary.unique_media_count;
                media = media_cache.emplace(media_part, projection).first;
                summary.peak_retained_media_count = std::max(
                    summary.peak_retained_media_count, media_cache.size());
            }
            view.index = summary.image_count;
            view.format = media->second.format;
            view.encoded_size_bytes = media->second.encoded_size_bytes;
            ++summary.image_count;
            if (callbacks.on_image) {
                callbacks.on_image(view);
            }
        });

    BoundedXmlCallbacks xml_callbacks;
    xml_callbacks.on_text = [&drawing_reader](std::string_view text) {
        drawing_reader.consume_text(text);
    };
    xml_callbacks.on_tag = [&drawing_reader](std::string_view tag) {
        drawing_reader.consume_tag(tag);
    };
    xml_callbacks.on_special_markup = [&drawing_reader] {
        drawing_reader.consume_special_markup();
    };
    scan_bounded_xml_from_chunk_source(
        package.entry_chunk_source(drawing_part.zip_path()), xml_callbacks,
        options.max_xml_window_bytes, "worksheet drawing");
    drawing_reader.finish();
    return summary;
}

} // namespace fastxlsx::detail
