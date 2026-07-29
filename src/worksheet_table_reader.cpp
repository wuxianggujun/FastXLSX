#include "worksheet_table_reader.hpp"

#include "bounded_xml_reader.hpp"
#include "package_reader.hpp"

#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
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
constexpr std::string_view table_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table";
constexpr std::string_view table_content_type =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml";
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
                "worksheet table reader contains an empty " + std::string(context));
        }
        return QualifiedName {{}, name};
    }
    if (separator == 0 || separator + 1U == name.size()
        || name.find(':', separator + 1U) != std::string_view::npos) {
        throw FastXlsxError(
            "worksheet table reader contains an invalid qualified "
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
        throw FastXlsxError("worksheet table reader contains an invalid XML tag");
    }
    if (raw_xml[1] == '!' || raw_xml[1] == '?') {
        throw FastXlsxError(
            "worksheet table reader received unsupported declaration markup as a tag");
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
        throw FastXlsxError("worksheet table reader contains an empty element name");
    }
    result.qualified_name = raw_xml.substr(name_begin, position - name_begin);
    const QualifiedName element_name = parse_qualified_name(
        result.qualified_name, "element name");
    result.local_name = element_name.local_name;
    result.prefix = element_name.prefix;

    if (result.closing) {
        while (position < raw_xml.size() - 1U
            && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position != raw_xml.size() - 1U) {
            throw FastXlsxError(
                "worksheet table reader closing tag contains attributes");
        }
        return result;
    }

    while (position < raw_xml.size()) {
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet table reader contains an incomplete XML tag");
        }
        if (raw_xml[position] == '>') {
            if (position + 1U != raw_xml.size()) {
                throw FastXlsxError(
                    "worksheet table reader contains trailing XML tag bytes");
            }
            return result;
        }
        if (raw_xml[position] == '/') {
            ++position;
            while (position < raw_xml.size() - 1U
                && is_xml_space(raw_xml[position])) {
                ++position;
            }
            if (position != raw_xml.size() - 1U || raw_xml[position] != '>') {
                throw FastXlsxError(
                    "worksheet table reader contains an invalid self-closing tag tail");
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
                "worksheet table reader contains an empty attribute name");
        }
        const std::string_view attribute_name =
            raw_xml.substr(attribute_begin, position - attribute_begin);
        (void)parse_qualified_name(attribute_name, "attribute name");
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size() || raw_xml[position] != '=') {
            throw FastXlsxError(
                "worksheet table reader contains an attribute without a value");
        }
        ++position;
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()
            || (raw_xml[position] != '"' && raw_xml[position] != '\'')) {
            throw FastXlsxError(
                "worksheet table reader contains an unquoted attribute value");
        }
        const char quote = raw_xml[position++];
        const std::size_t value_begin = position;
        while (position < raw_xml.size() && raw_xml[position] != quote) {
            if (raw_xml[position] == '<') {
                throw FastXlsxError(
                    "worksheet table reader attribute contains an invalid '<' byte");
            }
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet table reader contains an unterminated attribute value");
        }
        const std::string_view attribute_value =
            raw_xml.substr(value_begin, position - value_begin);
        ++position;
        if (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '/' && raw_xml[position] != '>') {
            throw FastXlsxError(
                "worksheet table reader attributes are not separated by whitespace");
        }
        const auto duplicate = std::find_if(result.attributes.begin(),
            result.attributes.end(), [attribute_name](const auto& attribute) {
                return attribute.first == attribute_name;
            });
        if (duplicate != result.attributes.end()) {
            throw FastXlsxError(
                "worksheet table reader contains a duplicate attribute");
        }
        result.attributes.emplace_back(attribute_name, attribute_value);
    }

    throw FastXlsxError("worksheet table reader contains an incomplete XML tag");
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
            "worksheet table reader contains an invalid XML character in "
            + std::string(label));
    }
    std::size_t byte_count = 1;
    if (code_point > 0x7FU) {
        byte_count = code_point <= 0x7FFU ? 2U
            : (code_point <= 0xFFFFU ? 3U : 4U);
    }
    if (output.size() > limit || byte_count > limit - output.size()) {
        throw FastXlsxError(
            "worksheet table reader " + std::string(label)
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
            "worksheet table reader contains an unknown XML entity in "
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
            "worksheet table reader contains an invalid XML entity in "
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
                "worksheet table reader contains an invalid XML entity in "
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
                    "worksheet table reader contains an invalid XML character in "
                    + std::string(label));
            }
            if (output.size() >= limit) {
                throw FastXlsxError(
                    "worksheet table reader " + std::string(label)
                    + " exceeds its configured text limit");
            }
            output.push_back(is_xml_space(raw[position]) ? ' ' : raw[position]);
            ++position;
            continue;
        }

        const std::size_t semicolon = raw.find(';', position + 1U);
        if (semicolon == std::string_view::npos) {
            throw FastXlsxError(
                "worksheet table reader contains an unterminated XML entity in "
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
            "worksheet table reader has an invalid " + std::string(label));
    }
    std::uint64_t result = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            throw FastXlsxError(
                "worksheet table reader has an invalid " + std::string(label));
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            throw FastXlsxError(
                "worksheet table reader has an invalid " + std::string(label));
        }
        result = result * 10U + digit;
    }
    return result;
}

bool parse_boolean(std::string_view value, std::string_view label)
{
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    throw FastXlsxError(
        "worksheet table reader has an invalid " + std::string(label));
}

struct A1Coordinate {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

std::optional<A1Coordinate> parse_a1_coordinate(std::string_view reference)
{
    std::size_t position = 0;
    if (position < reference.size() && reference[position] == '$') {
        ++position;
    }
    std::uint32_t column = 0;
    std::size_t column_count = 0;
    while (position < reference.size()) {
        const char character = reference[position];
        std::uint32_t digit = 0;
        if (character >= 'A' && character <= 'Z') {
            digit = static_cast<std::uint32_t>(character - 'A' + 1);
        } else if (character >= 'a' && character <= 'z') {
            digit = static_cast<std::uint32_t>(character - 'a' + 1);
        } else {
            break;
        }
        if (column > (max_excel_columns - digit) / 26U) {
            return std::nullopt;
        }
        column = column * 26U + digit;
        ++position;
        ++column_count;
    }
    if (column_count == 0 || column == 0 || column > max_excel_columns) {
        return std::nullopt;
    }
    if (position < reference.size() && reference[position] == '$') {
        ++position;
    }
    const std::size_t row_begin = position;
    std::uint32_t row = 0;
    while (position < reference.size()
        && reference[position] >= '0' && reference[position] <= '9') {
        const std::uint32_t digit =
            static_cast<std::uint32_t>(reference[position] - '0');
        if (row > (max_excel_rows - digit) / 10U) {
            return std::nullopt;
        }
        row = row * 10U + digit;
        ++position;
    }
    if (position != reference.size() || position == row_begin
        || row == 0 || row > max_excel_rows) {
        return std::nullopt;
    }
    return A1Coordinate {row, column};
}

std::optional<CellRange> parse_a1_range(std::string_view reference)
{
    const std::size_t separator = reference.find(':');
    if (separator != std::string_view::npos
        && reference.find(':', separator + 1U) != std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<A1Coordinate> first = parse_a1_coordinate(
        separator == std::string_view::npos ? reference
                                            : reference.substr(0, separator));
    const std::optional<A1Coordinate> last = separator == std::string_view::npos
        ? first
        : parse_a1_coordinate(reference.substr(separator + 1U));
    if (!first.has_value() || !last.has_value() || first->row > last->row
        || first->column > last->column) {
        return std::nullopt;
    }
    return CellRange {
        first->row, first->column, last->row, last->column};
}

bool same_range(const CellRange& left, const CellRange& right) noexcept
{
    return left.first_row == right.first_row
        && left.first_column == right.first_column
        && left.last_row == right.last_row
        && left.last_column == right.last_column;
}

std::string ascii_lower_copy(std::string_view value)
{
    std::string output(value);
    std::transform(output.begin(), output.end(), output.begin(), [](char character) {
        return character >= 'A' && character <= 'Z'
            ? static_cast<char>(character - 'A' + 'a')
            : character;
    });
    return output;
}

bool is_namespace_declaration(std::string_view name) noexcept
{
    return name == "xmlns" || name.starts_with("xmlns:");
}

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

struct NamespaceChange {
    std::string prefix;
    bool was_relationship_namespace = false;
};

enum class WorksheetFrameRole {
    Generic,
    TableParts,
    TablePart,
};

struct WorksheetFrame {
    std::string local_name;
    std::string prefix;
    WorksheetFrameRole role = WorksheetFrameRole::Generic;
    std::vector<NamespaceChange> namespace_changes;
};

class WorksheetTableReferenceReader {
public:
    WorksheetTableReferenceReader(
        WorksheetTableReaderOptions options, WorksheetTableReadSummary& summary)
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
                    "worksheet tableParts contains unsupported non-element content");
            }
            return;
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::ProcessingInstruction:
            if (inside_target()) {
                throw FastXlsxError(
                    "worksheet tableParts contains unsupported nested markup");
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

    [[nodiscard]] std::vector<std::string> finish()
    {
        if (!saw_worksheet_start_ || !saw_sheet_data_start_
            || !saw_sheet_data_end_ || !saw_worksheet_end_) {
            throw FastXlsxError(
                "worksheet table reader requires a worksheet root and closed sheetData");
        }
        if (!stack_.empty()) {
            throw FastXlsxError(
                "worksheet table reader ended inside an open worksheet element");
        }
        if (declared_table_count_.has_value()
            && *declared_table_count_ != relationship_ids_.size()) {
            throw FastXlsxError(
                "worksheet tableParts count does not match direct tablePart children");
        }
        return std::move(relationship_ids_);
    }

private:
    void consume_worksheet_start(const WorksheetEvent& event)
    {
        if (saw_worksheet_start_) {
            throw FastXlsxError("worksheet table reader found duplicate worksheet roots");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "worksheet" || tag.self_closing) {
            throw FastXlsxError("worksheet table reader found an invalid worksheet root");
        }
        root_prefix_ = std::string(tag.prefix);
        (void)apply_namespace_declarations(tag);
        saw_worksheet_start_ = true;
    }

    void consume_worksheet_end(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.self_closing || !tag.closing || tag.local_name != "worksheet"
            || tag.prefix != root_prefix_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet table reader worksheet root QName is mismatched");
        }
        saw_worksheet_end_ = true;
    }

    void consume_sheet_data_start(const WorksheetEvent& event)
    {
        if (!saw_worksheet_start_ || saw_sheet_data_start_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet table reader found an invalid sheetData boundary");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "sheetData"
            || tag.prefix != root_prefix_) {
            throw FastXlsxError(
                "worksheet table reader sheetData QName is mismatched");
        }
        saw_sheet_data_start_ = true;
    }

    void consume_sheet_data_end(const WorksheetEvent& event)
    {
        if (!saw_sheet_data_start_ || saw_sheet_data_end_) {
            throw FastXlsxError(
                "worksheet table reader found a duplicate sheetData end");
        }
        if (!event.self_closing) {
            const ParsedTag tag = parse_tag(event.raw_xml);
            if (!tag.closing || tag.local_name != "sheetData"
                || tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet table reader sheetData QName is mismatched");
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
            throw FastXlsxError("worksheet tableParts contains unexpected text");
        }
        if (stack_.empty()) {
            throw FastXlsxError("worksheet table reader found unexpected worksheet text");
        }
    }

    bool inside_target() const noexcept
    {
        return std::any_of(stack_.begin(), stack_.end(), [](const WorksheetFrame& frame) {
            return frame.role != WorksheetFrameRole::Generic;
        });
    }

    void consume_metadata(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.local_name != event.element_name) {
            throw FastXlsxError(
                "worksheet table reader element local name is mismatched");
        }
        if (tag.closing) {
            close_metadata(tag);
        } else {
            open_metadata(tag);
        }
    }

    void close_metadata(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().local_name != tag.local_name
            || stack_.back().prefix != tag.prefix) {
            throw FastXlsxError(
                "worksheet table reader contains mismatched element QName nesting");
        }
        WorksheetFrame frame = std::move(stack_.back());
        stack_.pop_back();
        restore_namespace_declarations(frame.namespace_changes);
    }

    void enforce_top_level_schema(const ParsedTag& tag)
    {
        if (!stack_.empty()) {
            return;
        }
        if (!saw_sheet_data_start_) {
            const std::optional<int> rank = prefix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet table reader found unsupported metadata before sheetData");
            }
            if (*rank < last_prefix_rank_) {
                throw FastXlsxError(
                    "worksheet table reader prefix elements are not in schema order");
            }
            last_prefix_rank_ = *rank;
        } else if (saw_sheet_data_end_) {
            const std::optional<int> rank = suffix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet table reader found unsupported worksheet suffix metadata");
            }
            if (*rank < last_suffix_rank_) {
                throw FastXlsxError(
                    "worksheet table reader suffix elements are not in schema order");
            }
            last_suffix_rank_ = *rank;
        } else {
            throw FastXlsxError(
                "worksheet tableParts appears inside sheetData");
        }
        if (tag.prefix != root_prefix_) {
            throw FastXlsxError(
                "worksheet table reader top-level QName differs from worksheet root");
        }
    }

    void open_metadata(const ParsedTag& tag)
    {
        enforce_top_level_schema(tag);
        std::vector<NamespaceChange> namespace_changes =
            apply_namespace_declarations(tag);
        if (tag.prefix == root_prefix_ && tag.local_name == "tableParts") {
            open_table_parts(tag, std::move(namespace_changes));
            return;
        }
        if (tag.prefix == root_prefix_ && tag.local_name == "tablePart") {
            open_table_part(tag, std::move(namespace_changes));
            return;
        }
        if (inside_target()) {
            throw FastXlsxError(
                "worksheet tableParts contains an unsupported child element");
        }
        open_generic(tag, std::move(namespace_changes));
    }

    void open_table_parts(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (!stack_.empty() || saw_table_parts_) {
            throw FastXlsxError(
                "worksheet table reader found duplicate or nested tableParts");
        }
        bool saw_count = false;
        std::uint64_t count = 0;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name =
                parse_qualified_name(name, "attribute name");
            if (!attribute_name.prefix.empty()
                || attribute_name.local_name != "count") {
                throw FastXlsxError(
                    "worksheet tableParts has an unsupported attribute");
            }
            count = parse_unsigned_decimal(raw_value, "tableParts count");
            if (count > options_.max_table_count) {
                throw FastXlsxError("worksheet tableParts exceeds max_table_count");
            }
            saw_count = true;
        }
        if (!saw_count) {
            throw FastXlsxError("worksheet tableParts requires count");
        }
        saw_table_parts_ = true;
        declared_table_count_ = count;
        if (tag.self_closing) {
            restore_namespace_declarations(namespace_changes);
            if (count != 0) {
                throw FastXlsxError(
                    "self-closing worksheet tableParts must have count zero");
            }
            return;
        }
        push_frame(tag, WorksheetFrameRole::TableParts,
            std::move(namespace_changes));
    }

    void open_table_part(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (stack_.empty()
            || stack_.back().role != WorksheetFrameRole::TableParts) {
            throw FastXlsxError(
                "worksheet tablePart is not a direct tableParts child");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError(
                "worksheet tablePart QName differs from tableParts");
        }
        if (relationship_ids_.size() >= options_.max_table_count) {
            throw FastXlsxError("worksheet tablePart exceeds max_table_count");
        }

        std::optional<std::string> relationship_id;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name =
                parse_qualified_name(name, "attribute name");
            if (attribute_name.local_name != "id"
                || attribute_name.prefix.empty()
                || !is_relationship_prefix(attribute_name.prefix)) {
                throw FastXlsxError(
                    "worksheet tablePart requires an OpenXML relationship id");
            }
            if (relationship_id.has_value()) {
                throw FastXlsxError(
                    "worksheet tablePart contains duplicate semantic relationship ids");
            }
            relationship_id = decode_xml_value(raw_value,
                options_.max_relationship_id_bytes,
                "relationship id (max_relationship_id_bytes)");
        }
        if (!relationship_id.has_value() || relationship_id->empty()) {
            throw FastXlsxError("worksheet tablePart relationship id cannot be empty");
        }
        if (std::find(relationship_ids_.begin(), relationship_ids_.end(),
                *relationship_id) != relationship_ids_.end()) {
            throw FastXlsxError(
                "worksheet tablePart relationship ids must be unique");
        }
        summary_.peak_relationship_id_bytes = std::max(
            summary_.peak_relationship_id_bytes, relationship_id->size());
        relationship_ids_.push_back(std::move(*relationship_id));
        summary_.peak_retained_relationship_count = std::max(
            summary_.peak_retained_relationship_count, relationship_ids_.size());

        if (tag.self_closing) {
            restore_namespace_declarations(namespace_changes);
            return;
        }
        push_frame(tag, WorksheetFrameRole::TablePart,
            std::move(namespace_changes));
    }

    void open_generic(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (tag.self_closing) {
            restore_namespace_declarations(namespace_changes);
            return;
        }
        push_frame(tag, WorksheetFrameRole::Generic,
            std::move(namespace_changes));
    }

    void push_frame(const ParsedTag& tag, WorksheetFrameRole role,
        std::vector<NamespaceChange> namespace_changes)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            throw FastXlsxError(
                "worksheet table reader exceeds max_xml_nesting_depth");
        }
        stack_.push_back(WorksheetFrame {std::string(tag.local_name),
            std::string(tag.prefix), role, std::move(namespace_changes)});
        summary_.peak_xml_nesting_depth = std::max(
            summary_.peak_xml_nesting_depth, stack_.size());
    }

    bool is_relationship_prefix(std::string_view prefix) const noexcept
    {
        return std::find(relationship_prefixes_.begin(),
            relationship_prefixes_.end(), prefix) != relationship_prefixes_.end();
    }

    void set_relationship_prefix(std::string_view prefix, bool enabled)
    {
        const auto found = std::find(relationship_prefixes_.begin(),
            relationship_prefixes_.end(), prefix);
        if (enabled && found == relationship_prefixes_.end()) {
            relationship_prefixes_.emplace_back(prefix);
        } else if (!enabled && found != relationship_prefixes_.end()) {
            relationship_prefixes_.erase(found);
        }
    }

    std::vector<NamespaceChange> apply_namespace_declarations(const ParsedTag& tag)
    {
        std::vector<NamespaceChange> changes;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (!name.starts_with("xmlns:")) {
                continue;
            }
            const std::string_view prefix = name.substr(6U);
            const bool previous = is_relationship_prefix(prefix);
            const std::string uri = decode_xml_value(raw_value,
                options_.max_xml_window_bytes, "namespace URI");
            changes.push_back(NamespaceChange {std::string(prefix), previous});
            set_relationship_prefix(prefix, uri == relationship_namespace);
        }
        return changes;
    }

    void restore_namespace_declarations(
        const std::vector<NamespaceChange>& changes)
    {
        for (auto item = changes.rbegin(); item != changes.rend(); ++item) {
            set_relationship_prefix(item->prefix,
                item->was_relationship_namespace);
        }
    }

    WorksheetTableReaderOptions options_;
    WorksheetTableReadSummary& summary_;
    std::vector<WorksheetFrame> stack_;
    std::vector<std::string> relationship_ids_;
    std::vector<std::string> relationship_prefixes_;
    std::optional<std::uint64_t> declared_table_count_;
    std::string root_prefix_;
    int last_prefix_rank_ = 0;
    int last_suffix_rank_ = 0;
    bool saw_worksheet_start_ = false;
    bool saw_sheet_data_start_ = false;
    bool saw_sheet_data_end_ = false;
    bool saw_worksheet_end_ = false;
    bool saw_table_parts_ = false;
    bool inside_cell_ = false;
};

enum class TableFrameRole {
    Root,
    AutoFilter,
    TableColumns,
    TableColumn,
    TableStyleInfo,
};

struct TableFrame {
    std::string qualified_name;
    TableFrameRole role = TableFrameRole::Root;
};

class TablePartProjectionReader {
public:
    TablePartProjectionReader(
        WorksheetTableReaderOptions options, WorksheetTableReadSummary& summary)
        : options_(options)
        , summary_(summary)
    {
    }

    void consume_text(std::string_view text) const
    {
        std::size_t offset = 0;
        if (!saw_root_ && text.substr(0, 3) == "\xef\xbb\xbf") {
            offset = 3;
        }
        if (has_non_whitespace(text.substr(offset))) {
            throw FastXlsxError(
                "worksheet table part contains unsupported element text");
        }
    }

    void consume_special_markup() const
    {
        if (saw_root_ && !finished_root_) {
            throw FastXlsxError(
                "worksheet table part contains unsupported nested markup");
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

    [[nodiscard]] WorksheetTableView finish()
    {
        if (!saw_root_ || !finished_root_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet table reader requires one closed table root");
        }
        if (!saw_table_columns_) {
            throw FastXlsxError("worksheet table part requires tableColumns");
        }
        if (!declared_column_count_.has_value()
            || *declared_column_count_ != view_.columns.size()) {
            throw FastXlsxError(
                "worksheet tableColumns count does not match direct tableColumn children");
        }
        const std::uint64_t width = static_cast<std::uint64_t>(view_.range.last_column)
            - view_.range.first_column + 1U;
        if (view_.columns.size() != width) {
            throw FastXlsxError(
                "worksheet table column count does not match table range width");
        }
        if (view_.auto_filter_range.has_value()
            && !same_range(*view_.auto_filter_range, view_.range)) {
            throw FastXlsxError(
                "worksheet table autoFilter boundary must match the table range when totals rows are absent");
        }
        return std::move(view_);
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
                "worksheet table part contains multiple root elements");
        }
        if (stack_.back().role == TableFrameRole::AutoFilter
            || stack_.back().role == TableFrameRole::TableColumn
            || stack_.back().role == TableFrameRole::TableStyleInfo) {
            throw FastXlsxError(
                "worksheet table part contains unsupported nested table semantics");
        }
        if (tag.prefix != root_prefix_) {
            throw FastXlsxError(
                "worksheet table part target QName differs from the table root");
        }
        validate_target_namespace(tag, false);
        if (stack_.back().role == TableFrameRole::Root) {
            open_root_child(tag);
            return;
        }
        if (stack_.back().role == TableFrameRole::TableColumns
            && tag.local_name == "tableColumn") {
            open_table_column(tag);
            return;
        }
        throw FastXlsxError(
            "worksheet table part contains an unsupported child element");
    }

    void open_root(const ParsedTag& tag)
    {
        if (tag.local_name != "table" || tag.closing || tag.self_closing) {
            throw FastXlsxError("worksheet table reader requires a non-empty table root");
        }
        root_prefix_ = std::string(tag.prefix);
        validate_target_namespace(tag, true);

        bool saw_id = false;
        bool saw_name = false;
        bool saw_display_name = false;
        bool saw_reference = false;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name =
                parse_qualified_name(name, "attribute name");
            if (!attribute_name.prefix.empty()) {
                throw FastXlsxError(
                    "worksheet table root has an unsupported qualified attribute");
            }
            if (attribute_name.local_name == "id") {
                const std::uint64_t id = parse_unsigned_decimal(raw_value, "table id");
                if (id == 0 || id > std::numeric_limits<std::uint32_t>::max()) {
                    throw FastXlsxError("worksheet table id is outside uint32 range");
                }
                saw_id = true;
            } else if (attribute_name.local_name == "name") {
                view_.name = decode_xml_value(raw_value,
                    options_.max_table_name_bytes,
                    "table name (max_table_name_bytes)");
                saw_name = true;
            } else if (attribute_name.local_name == "displayName") {
                view_.display_name = decode_xml_value(raw_value,
                    options_.max_table_name_bytes,
                    "table displayName (max_table_name_bytes)");
                saw_display_name = true;
            } else if (attribute_name.local_name == "ref") {
                const std::string reference = decode_xml_value(raw_value,
                    options_.max_range_reference_bytes,
                    "table ref (max_range_reference_bytes)");
                summary_.peak_range_reference_bytes = std::max(
                    summary_.peak_range_reference_bytes, reference.size());
                const std::optional<CellRange> range = parse_a1_range(reference);
                if (!range.has_value() || range->last_row == range->first_row) {
                    throw FastXlsxError(
                        "worksheet table ref must include a header and data row");
                }
                view_.range = *range;
                saw_reference = true;
            } else if (attribute_name.local_name == "headerRowCount") {
                if (parse_unsigned_decimal(raw_value, "headerRowCount") != 1U) {
                    throw FastXlsxError(
                        "worksheet table reader supports exactly one header row");
                }
            } else if (attribute_name.local_name == "totalsRowCount") {
                if (parse_unsigned_decimal(raw_value, "totalsRowCount") != 0U) {
                    throw FastXlsxError(
                        "worksheet table reader does not support totals rows");
                }
            } else if (attribute_name.local_name == "totalsRowShown") {
                if (parse_boolean(raw_value, "totalsRowShown")) {
                    throw FastXlsxError(
                        "worksheet table reader does not support totals rows");
                }
            } else {
                throw FastXlsxError(
                    "worksheet table root has an unsupported attribute");
            }
        }
        if (!saw_id || !saw_name || view_.name.empty()
            || !saw_display_name || view_.display_name.empty() || !saw_reference) {
            throw FastXlsxError(
                "worksheet table root requires id, name, displayName, and ref");
        }
        const std::uint64_t width = static_cast<std::uint64_t>(view_.range.last_column)
            - view_.range.first_column + 1U;
        if (width > options_.max_columns_per_table) {
            throw FastXlsxError(
                "worksheet table range exceeds max_columns_per_table");
        }
        summary_.peak_table_name_bytes = std::max({
            summary_.peak_table_name_bytes,
            view_.name.size(),
            view_.display_name.size(),
        });
        saw_root_ = true;
        push_frame(tag, TableFrameRole::Root);
    }

    void validate_target_namespace(const ParsedTag& tag, bool require_declaration) const
    {
        const std::string expected_declaration = root_prefix_.empty()
            ? std::string("xmlns")
            : std::string("xmlns:") + root_prefix_;
        bool saw_declaration = false;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (name != expected_declaration) {
                continue;
            }
            const std::string uri = decode_xml_value(raw_value,
                options_.max_xml_window_bytes, "table namespace URI");
            if (uri != spreadsheet_namespace) {
                throw FastXlsxError(
                    "worksheet table root is not in the spreadsheet namespace");
            }
            saw_declaration = true;
        }
        if (require_declaration && !saw_declaration) {
            throw FastXlsxError(
                "worksheet table root requires the spreadsheet namespace declaration");
        }
    }

    void open_root_child(const ParsedTag& tag)
    {
        int rank = 0;
        if (tag.local_name == "autoFilter") {
            rank = 1;
        } else if (tag.local_name == "sortState") {
            throw FastXlsxError(
                "worksheet table reader does not support table sort/filter criteria");
        } else if (tag.local_name == "tableColumns") {
            rank = 3;
        } else if (tag.local_name == "tableStyleInfo") {
            rank = 4;
        } else if (tag.local_name == "extLst") {
            throw FastXlsxError(
                "worksheet table reader does not support table extensions");
        } else {
            throw FastXlsxError(
                "worksheet table part contains an unsupported direct child");
        }
        if (rank < last_root_child_rank_) {
            throw FastXlsxError(
                "worksheet table part children are not in schema order");
        }
        last_root_child_rank_ = rank;

        if (tag.local_name == "autoFilter") {
            open_auto_filter(tag);
        } else if (tag.local_name == "tableColumns") {
            open_table_columns(tag);
        } else {
            open_table_style_info(tag);
        }
    }

    void open_auto_filter(const ParsedTag& tag)
    {
        if (saw_auto_filter_) {
            throw FastXlsxError("worksheet table part contains duplicate autoFilter");
        }
        bool saw_reference = false;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name =
                parse_qualified_name(name, "attribute name");
            if (!attribute_name.prefix.empty()
                || attribute_name.local_name != "ref") {
                throw FastXlsxError(
                    "worksheet table autoFilter has an unsupported attribute");
            }
            const std::string reference = decode_xml_value(raw_value,
                options_.max_range_reference_bytes,
                "table autoFilter ref (max_range_reference_bytes)");
            summary_.peak_range_reference_bytes = std::max(
                summary_.peak_range_reference_bytes, reference.size());
            const std::optional<CellRange> range = parse_a1_range(reference);
            if (!range.has_value()) {
                throw FastXlsxError(
                    "worksheet table autoFilter ref is not a valid A1 range");
            }
            view_.auto_filter_range = *range;
            saw_reference = true;
        }
        if (!saw_reference) {
            throw FastXlsxError("worksheet table autoFilter requires ref");
        }
        saw_auto_filter_ = true;
        if (!tag.self_closing) {
            push_frame(tag, TableFrameRole::AutoFilter);
        }
    }

    void open_table_columns(const ParsedTag& tag)
    {
        if (saw_table_columns_) {
            throw FastXlsxError("worksheet table part contains duplicate tableColumns");
        }
        bool saw_count = false;
        std::uint64_t count = 0;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name =
                parse_qualified_name(name, "attribute name");
            if (!attribute_name.prefix.empty()
                || attribute_name.local_name != "count") {
                throw FastXlsxError(
                    "worksheet tableColumns has an unsupported attribute");
            }
            count = parse_unsigned_decimal(raw_value, "tableColumns count");
            if (count > options_.max_columns_per_table) {
                throw FastXlsxError(
                    "worksheet tableColumns exceeds max_columns_per_table");
            }
            saw_count = true;
        }
        if (!saw_count) {
            throw FastXlsxError("worksheet tableColumns requires count");
        }
        saw_table_columns_ = true;
        declared_column_count_ = count;
        if (!tag.self_closing) {
            push_frame(tag, TableFrameRole::TableColumns);
        }
    }

    void open_table_column(const ParsedTag& tag)
    {
        if (view_.columns.size() >= options_.max_columns_per_table) {
            throw FastXlsxError(
                "worksheet tableColumn exceeds max_columns_per_table");
        }
        WorksheetTableColumnView column;
        bool saw_id = false;
        bool saw_name = false;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name =
                parse_qualified_name(name, "attribute name");
            if (!attribute_name.prefix.empty()) {
                throw FastXlsxError(
                    "worksheet tableColumn has an unsupported qualified attribute");
            }
            if (attribute_name.local_name == "id") {
                const std::uint64_t id = parse_unsigned_decimal(
                    raw_value, "tableColumn id");
                if (id == 0 || id > std::numeric_limits<std::uint32_t>::max()) {
                    throw FastXlsxError(
                        "worksheet tableColumn id is outside uint32 range");
                }
                column.id = static_cast<std::uint32_t>(id);
                saw_id = true;
            } else if (attribute_name.local_name == "name") {
                column.name = decode_xml_value(raw_value,
                    options_.max_column_name_bytes,
                    "tableColumn name (max_column_name_bytes)");
                saw_name = true;
            } else if (attribute_name.local_name == "totalsRowLabel"
                || attribute_name.local_name == "totalsRowFunction"
                || attribute_name.local_name == "calculatedColumnFormula"
                || attribute_name.local_name == "totalsRowFormula") {
                throw FastXlsxError(
                    "worksheet table reader does not support totals or calculated column metadata");
            } else {
                throw FastXlsxError(
                    "worksheet tableColumn has an unsupported attribute");
            }
        }
        if (!saw_id || !saw_name || column.name.empty()) {
            throw FastXlsxError(
                "worksheet tableColumn requires a positive id and non-empty name");
        }
        if (!column_ids_.insert(column.id).second) {
            throw FastXlsxError("worksheet tableColumn ids must be unique");
        }
        if (!column_names_.insert(ascii_lower_copy(column.name)).second) {
            throw FastXlsxError("worksheet tableColumn names must be unique");
        }
        summary_.peak_column_name_bytes = std::max(
            summary_.peak_column_name_bytes, column.name.size());
        view_.columns.push_back(std::move(column));
        summary_.peak_columns_per_table = std::max(
            summary_.peak_columns_per_table, view_.columns.size());
        if (!tag.self_closing) {
            push_frame(tag, TableFrameRole::TableColumn);
        }
    }

    void open_table_style_info(const ParsedTag& tag)
    {
        if (saw_table_style_info_) {
            throw FastXlsxError(
                "worksheet table part contains duplicate tableStyleInfo");
        }
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name =
                parse_qualified_name(name, "attribute name");
            if (!attribute_name.prefix.empty()) {
                throw FastXlsxError(
                    "worksheet tableStyleInfo has an unsupported qualified attribute");
            }
            if (attribute_name.local_name == "name") {
                (void)decode_xml_value(raw_value,
                    options_.max_table_name_bytes, "table style name");
            } else if (attribute_name.local_name == "showFirstColumn"
                || attribute_name.local_name == "showLastColumn"
                || attribute_name.local_name == "showRowStripes"
                || attribute_name.local_name == "showColumnStripes") {
                (void)parse_boolean(raw_value, "tableStyleInfo boolean");
            } else {
                throw FastXlsxError(
                    "worksheet tableStyleInfo has an unsupported attribute");
            }
        }
        saw_table_style_info_ = true;
        if (!tag.self_closing) {
            push_frame(tag, TableFrameRole::TableStyleInfo);
        }
    }

    void close_tag(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().qualified_name != tag.qualified_name) {
            throw FastXlsxError(
                "worksheet table part contains mismatched element QName nesting");
        }
        const TableFrameRole role = stack_.back().role;
        stack_.pop_back();
        if (role == TableFrameRole::Root) {
            if (!stack_.empty() || tag.local_name != "table") {
                throw FastXlsxError(
                    "worksheet table part contains an invalid table root close");
            }
            finished_root_ = true;
        }
    }

    void push_frame(const ParsedTag& tag, TableFrameRole role)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            throw FastXlsxError(
                "worksheet table reader exceeds max_xml_nesting_depth");
        }
        stack_.push_back(TableFrame {std::string(tag.qualified_name), role});
        summary_.peak_xml_nesting_depth = std::max(
            summary_.peak_xml_nesting_depth, stack_.size());
    }

    WorksheetTableReaderOptions options_;
    WorksheetTableReadSummary& summary_;
    WorksheetTableView view_;
    std::vector<TableFrame> stack_;
    std::set<std::uint32_t> column_ids_;
    std::set<std::string> column_names_;
    std::optional<std::uint64_t> declared_column_count_;
    std::string root_prefix_;
    int last_root_child_rank_ = 0;
    bool saw_root_ = false;
    bool finished_root_ = false;
    bool saw_auto_filter_ = false;
    bool saw_table_columns_ = false;
    bool saw_table_style_info_ = false;
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
    std::string_view target, const WorksheetTableReaderOptions& options,
    WorksheetTableReadSummary& summary)
{
    if (target.empty()) {
        throw FastXlsxError(
            "worksheet table relationship target cannot be empty");
    }
    if (target.size() > options.max_relationship_target_bytes) {
        throw FastXlsxError(
            "worksheet table relationship target exceeds max_relationship_target_bytes");
    }
    std::string decoded;
    decoded.reserve(target.size());
    for (std::size_t index = 0; index < target.size(); ++index) {
        if (target[index] != '%') {
            if (target[index] == '\0') {
                throw FastXlsxError(
                    "worksheet table relationship target contains a null byte");
            }
            decoded.push_back(target[index]);
            continue;
        }
        if (index + 2U >= target.size()) {
            throw FastXlsxError(
                "worksheet table relationship target has invalid percent encoding");
        }
        const int high = hex_digit_value(target[index + 1U]);
        const int low = hex_digit_value(target[index + 2U]);
        if (high < 0 || low < 0) {
            throw FastXlsxError(
                "worksheet table relationship target has invalid percent encoding");
        }
        const char decoded_byte = static_cast<char>((high << 4) | low);
        if (decoded_byte == '\0') {
            throw FastXlsxError(
                "worksheet table relationship target decodes to a null byte");
        }
        decoded.push_back(decoded_byte);
        index += 2U;
    }
    summary.peak_relationship_target_bytes = std::max(
        summary.peak_relationship_target_bytes, decoded.size());
    return decoded;
}

PartName resolve_table_part(const PartName& worksheet_part,
    const Relationship& relationship, const WorksheetTableReaderOptions& options,
    WorksheetTableReadSummary& summary)
{
    std::string target = decode_relationship_target(
        relationship.target, options, summary);
    if (target.find_first_of("?#") != std::string::npos) {
        throw FastXlsxError(
            "worksheet table relationship target cannot contain a query or fragment");
    }
    if (!target.empty() && target.front() == '/') {
        return PartName(target);
    }
    const std::string source = worksheet_part.value();
    const std::size_t slash = source.find_last_of('/');
    if (slash == std::string::npos) {
        throw FastXlsxError(
            "worksheet table relationship owner has no package directory");
    }
    return PartName(source.substr(0, slash) + "/" + target);
}

void validate_options(const WorksheetTableReaderOptions& options)
{
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError(
            "WorksheetTableReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError(
            "WorksheetTableReader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_table_count == 0) {
        throw FastXlsxError("WorksheetTableReader requires nonzero max_table_count");
    }
    if (options.max_relationship_id_bytes == 0) {
        throw FastXlsxError(
            "WorksheetTableReader requires nonzero max_relationship_id_bytes");
    }
    if (options.max_relationship_target_bytes == 0) {
        throw FastXlsxError(
            "WorksheetTableReader requires nonzero max_relationship_target_bytes");
    }
    if (options.max_table_name_bytes == 0) {
        throw FastXlsxError(
            "WorksheetTableReader requires nonzero max_table_name_bytes");
    }
    if (options.max_columns_per_table == 0) {
        throw FastXlsxError(
            "WorksheetTableReader requires nonzero max_columns_per_table");
    }
    if (options.max_column_name_bytes == 0) {
        throw FastXlsxError(
            "WorksheetTableReader requires nonzero max_column_name_bytes");
    }
    if (options.max_range_reference_bytes == 0) {
        throw FastXlsxError(
            "WorksheetTableReader requires nonzero max_range_reference_bytes");
    }
}

} // namespace

WorksheetTableReadSummary read_worksheet_tables_from_package(
    const PackageReader& package,
    const PartName& worksheet_part,
    const WorksheetTableReadCallbacks& callbacks,
    WorksheetTableReaderOptions options)
{
    validate_options(options);

    WorksheetTableReadSummary summary;
    WorksheetTableReferenceReader reference_reader(options, summary);
    WorksheetEventReaderOptions event_options;
    event_options.max_window_bytes = options.max_xml_window_bytes;
    event_options.copy_context_attributes = false;
    scan_worksheet_events_from_chunk_source(
        package.entry_chunk_source(worksheet_part.zip_path()),
        [&reference_reader](const WorksheetEvent& event) {
            reference_reader.consume(event);
        },
        event_options);
    std::vector<std::string> relationship_ids = reference_reader.finish();
    if (relationship_ids.empty()) {
        return summary;
    }

    const RelationshipSet* relationships = package.relationships_for(worksheet_part);
    if (relationships == nullptr) {
        throw FastXlsxError(
            "worksheet tableParts requires worksheet relationships");
    }

    std::set<PartName> target_parts;
    for (const std::string& relationship_id : relationship_ids) {
        const Relationship* relationship = relationships->find_by_id(relationship_id);
        if (relationship == nullptr) {
            throw FastXlsxError(
                "worksheet tablePart relationship id is missing");
        }
        if (relationship->type != table_relationship_type) {
            throw FastXlsxError(
                "worksheet tablePart relationship has the wrong type");
        }
        if (relationship->target_mode != Relationship::TargetMode::Internal) {
            throw FastXlsxError(
                "worksheet tablePart relationship must be internal");
        }
        const PartName table_part = resolve_table_part(
            worksheet_part, *relationship, options, summary);
        if (!target_parts.insert(table_part).second) {
            throw FastXlsxError(
                "worksheet tablePart relationships must target unique parts");
        }
        const PackagePart* indexed_part = package.part_index().find_part(table_part);
        if (indexed_part == nullptr) {
            throw FastXlsxError(
                "worksheet tablePart relationship targets an unknown part");
        }
        if (indexed_part->content_type != table_content_type) {
            throw FastXlsxError(
                "worksheet tablePart target has the wrong content type");
        }

        TablePartProjectionReader table_reader(options, summary);
        BoundedXmlCallbacks xml_callbacks;
        xml_callbacks.on_text = [&table_reader](std::string_view text) {
            table_reader.consume_text(text);
        };
        xml_callbacks.on_tag = [&table_reader](std::string_view tag) {
            table_reader.consume_tag(tag);
        };
        xml_callbacks.on_special_markup = [&table_reader] {
            table_reader.consume_special_markup();
        };
        scan_bounded_xml_from_chunk_source(
            package.entry_chunk_source(table_part.zip_path()),
            xml_callbacks,
            options.max_xml_window_bytes,
            "worksheet table part");

        WorksheetTableView view = table_reader.finish();
        view.index = summary.table_count;
        ++summary.table_count;
        summary.column_count += view.columns.size();
        if (view.auto_filter_range.has_value()) {
            ++summary.auto_filter_count;
        }
        if (callbacks.on_table) {
            callbacks.on_table(view);
        }
    }
    return summary;
}

} // namespace fastxlsx::detail
