#include "worksheet_hyperlink_reader.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr std::uint32_t max_excel_rows = 1048576U;
constexpr std::uint32_t max_excel_columns = 16384U;
constexpr std::string_view relationship_namespace =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr std::string_view hyperlink_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";

bool is_xml_space(char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\r'
        || character == '\n';
}

bool is_closing_tag(std::string_view raw_xml) noexcept
{
    return raw_xml.size() > 2 && raw_xml.front() == '<' && raw_xml[1] == '/';
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
                "worksheet hyperlink contains an empty " + std::string(context));
        }
        return QualifiedName {{}, name};
    }
    if (separator == 0 || separator + 1U == name.size()
        || name.find(':', separator + 1U) != std::string_view::npos) {
        throw FastXlsxError(
            "worksheet hyperlink contains an invalid qualified "
            + std::string(context));
    }
    return QualifiedName {
        name.substr(0, separator), name.substr(separator + 1U)};
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
        throw FastXlsxError("worksheet hyperlink contains an invalid XML tag");
    }
    if (raw_xml[1] == '!' || raw_xml[1] == '?') {
        throw FastXlsxError(
            "worksheet hyperlink received unsupported declaration markup");
    }

    ParsedTag result;
    result.closing = is_closing_tag(raw_xml);
    std::size_t position = result.closing ? 2U : 1U;
    const std::size_t name_begin = position;
    while (position < raw_xml.size()
        && !is_ascii_name_boundary(raw_xml[position])) {
        ++position;
    }
    if (position == name_begin) {
        throw FastXlsxError("worksheet hyperlink contains an empty element name");
    }
    result.qualified_name = raw_xml.substr(name_begin, position - name_begin);
    const QualifiedName element_name = parse_qualified_name(
        result.qualified_name, "element name");
    result.local_name = element_name.local_name;
    if (!element_name.prefix.empty()) {
        result.prefix = result.qualified_name.substr(
            0, element_name.prefix.size() + 1U);
    }

    if (result.closing) {
        while (position < raw_xml.size() - 1U
            && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position != raw_xml.size() - 1U || raw_xml[position] != '>') {
            throw FastXlsxError(
                "worksheet hyperlink closing tag contains attributes");
        }
        return result;
    }

    while (position < raw_xml.size()) {
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet hyperlink contains an incomplete XML tag");
        }
        if (raw_xml[position] == '>') {
            ++position;
            if (position != raw_xml.size()) {
                throw FastXlsxError(
                    "worksheet hyperlink contains trailing XML tag bytes");
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
                    "worksheet hyperlink contains an invalid self-closing tag tail");
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
                "worksheet hyperlink contains an empty attribute name");
        }
        const std::string_view attribute_name =
            raw_xml.substr(attribute_begin, position - attribute_begin);
        (void)parse_qualified_name(attribute_name, "attribute name");
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size() || raw_xml[position] != '=') {
            throw FastXlsxError(
                "worksheet hyperlink contains an attribute without a value");
        }
        ++position;
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()
            || (raw_xml[position] != '"' && raw_xml[position] != '\'')) {
            throw FastXlsxError(
                "worksheet hyperlink contains an unquoted attribute value");
        }
        const char quote = raw_xml[position++];
        const std::size_t value_begin = position;
        while (position < raw_xml.size() && raw_xml[position] != quote) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet hyperlink contains an unterminated attribute value");
        }
        const std::string_view attribute_value =
            raw_xml.substr(value_begin, position - value_begin);
        ++position;
        if (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '/' && raw_xml[position] != '>') {
            throw FastXlsxError(
                "worksheet hyperlink attributes are not separated by whitespace");
        }
        for (const auto& [existing_name, existing_value] : result.attributes) {
            (void)existing_value;
            if (existing_name == attribute_name) {
                throw FastXlsxError(
                    "worksheet hyperlink contains a duplicate attribute");
            }
        }
        result.attributes.emplace_back(attribute_name, attribute_value);
    }

    throw FastXlsxError("worksheet hyperlink contains an incomplete XML tag");
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
            "worksheet hyperlink contains an invalid XML character in "
            + std::string(label));
    }
    std::size_t byte_count = 1;
    if (code_point > 0x7FU) {
        byte_count = code_point <= 0x7FFU ? 2U
            : (code_point <= 0xFFFFU ? 3U : 4U);
    }
    if (output.size() > limit || byte_count > limit - output.size()) {
        throw FastXlsxError(
            "worksheet hyperlink " + std::string(label)
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
            "worksheet hyperlink contains an unknown XML entity in "
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
            "worksheet hyperlink contains an empty XML character reference");
    }
    std::uint64_t value = 0;
    for (; position < body.size(); ++position) {
        const char character = body[position];
        std::uint32_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint32_t>(character - '0');
        } else if (base == 16 && character >= 'a' && character <= 'f') {
            digit = static_cast<std::uint32_t>(character - 'a' + 10);
        } else if (base == 16 && character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint32_t>(character - 'A' + 10);
        } else {
            throw FastXlsxError(
                "worksheet hyperlink contains an invalid XML character reference");
        }
        if (digit >= static_cast<std::uint32_t>(base)
            || value > (0x10FFFFU - digit) / static_cast<std::uint32_t>(base)) {
            throw FastXlsxError(
                "worksheet hyperlink XML character reference overflows");
        }
        value = value * static_cast<std::uint32_t>(base) + digit;
    }
    return static_cast<std::uint32_t>(value);
}

void append_decoded_xml(std::string& output, std::string_view raw,
    std::size_t limit, std::string_view label, bool normalize_literal_whitespace)
{
    std::size_t position = 0;
    while (position < raw.size()) {
        if (raw[position] != '&') {
            const char character = normalize_literal_whitespace
                && is_xml_space(raw[position]) ? ' ' : raw[position];
            if (output.size() >= limit) {
                throw FastXlsxError(
                    "worksheet hyperlink " + std::string(label)
                    + " exceeds its configured text limit");
            }
            output.push_back(character);
            ++position;
            continue;
        }

        const std::size_t semicolon = raw.find(';', position + 1U);
        if (semicolon == std::string_view::npos) {
            throw FastXlsxError(
                "worksheet hyperlink contains an unterminated XML entity in "
                + std::string(label));
        }
        const std::string_view body = raw.substr(
            position + 1U, semicolon - position - 1U);
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

std::string decode_xml_value(std::string_view raw, std::size_t limit,
    std::string_view label, bool normalize_literal_whitespace = true)
{
    std::string decoded;
    decoded.reserve(std::min(raw.size(), limit));
    append_decoded_xml(
        decoded, raw, limit, label, normalize_literal_whitespace);
    return decoded;
}

struct A1Coordinate {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

std::optional<A1Coordinate> parse_a1_coordinate(std::string_view text)
{
    std::size_t position = 0;
    if (position < text.size() && text[position] == '$') {
        ++position;
    }
    std::uint64_t column = 0;
    const std::size_t column_begin = position;
    while (position < text.size()) {
        char character = text[position];
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
        if (character < 'A' || character > 'Z') {
            break;
        }
        column = column * 26U + static_cast<std::uint32_t>(character - 'A' + 1);
        if (column > max_excel_columns) {
            return std::nullopt;
        }
        ++position;
    }
    if (position == column_begin || column == 0) {
        return std::nullopt;
    }
    if (position < text.size() && text[position] == '$') {
        ++position;
    }
    std::uint64_t row = 0;
    const std::size_t row_begin = position;
    while (position < text.size() && text[position] >= '0'
        && text[position] <= '9') {
        row = row * 10U + static_cast<std::uint32_t>(text[position] - '0');
        if (row > max_excel_rows) {
            return std::nullopt;
        }
        ++position;
    }
    if (position == row_begin || position != text.size() || row == 0) {
        return std::nullopt;
    }
    return A1Coordinate {static_cast<std::uint32_t>(row),
        static_cast<std::uint32_t>(column)};
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

void audit_hyperlink_overlaps(const std::vector<CellRange>& ranges)
{
    std::vector<std::size_t> ordered_indices(ranges.size());
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        ordered_indices[index] = index;
    }
    std::sort(ordered_indices.begin(), ordered_indices.end(),
        [&ranges](std::size_t left, std::size_t right) {
            const CellRange& lhs = ranges[left];
            const CellRange& rhs = ranges[right];
            if (lhs.first_row != rhs.first_row) {
                return lhs.first_row < rhs.first_row;
            }
            if (lhs.first_column != rhs.first_column) {
                return lhs.first_column < rhs.first_column;
            }
            if (lhs.last_row != rhs.last_row) {
                return lhs.last_row < rhs.last_row;
            }
            return lhs.last_column < rhs.last_column;
        });

    std::multimap<std::uint32_t, std::size_t> active_by_last_row;
    std::map<std::uint32_t, std::size_t> active_by_first_column;
    for (const std::size_t current_index : ordered_indices) {
        const CellRange& current = ranges[current_index];
        while (!active_by_last_row.empty()
            && active_by_last_row.begin()->first < current.first_row) {
            const std::size_t expired_index = active_by_last_row.begin()->second;
            const auto active_column = active_by_first_column.find(
                ranges[expired_index].first_column);
            if (active_column == active_by_first_column.end()
                || active_column->second != expired_index) {
                throw FastXlsxError(
                    "worksheet hyperlink overlap audit lost an active range");
            }
            active_by_first_column.erase(active_column);
            active_by_last_row.erase(active_by_last_row.begin());
        }

        const auto next = active_by_first_column.lower_bound(current.first_column);
        if (next != active_by_first_column.end()
            && ranges[next->second].first_column <= current.last_column) {
            throw FastXlsxError(
                "worksheet hyperlink ranges overlap or duplicate");
        }
        if (next != active_by_first_column.begin()) {
            const auto previous = std::prev(next);
            if (ranges[previous->second].last_column >= current.first_column) {
                throw FastXlsxError(
                    "worksheet hyperlink ranges overlap or duplicate");
            }
        }
        active_by_first_column.emplace(current.first_column, current_index);
        active_by_last_row.emplace(current.last_row, current_index);
    }
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

enum class FrameRole {
    Generic,
    Hyperlinks,
    Hyperlink,
};

struct Frame {
    std::string local_name;
    std::string prefix;
    FrameRole role = FrameRole::Generic;
    std::vector<NamespaceChange> namespace_changes;
};

class WorksheetHyperlinkProjectionReader {
public:
    WorksheetHyperlinkProjectionReader(
        const RelationshipSet* worksheet_relationships,
        const WorksheetHyperlinkReadCallbacks& callbacks,
        WorksheetHyperlinkReaderOptions options,
        const WorksheetHyperlinkInternalCallbacks& internal_callbacks)
        : worksheet_relationships_(worksheet_relationships)
        , callbacks_(callbacks)
        , options_(options)
        , internal_callbacks_(internal_callbacks)
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
                    "worksheet hyperlink contains unsupported non-element content");
            }
            return;
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::ProcessingInstruction:
            if (inside_target()) {
                throw FastXlsxError(
                    "worksheet hyperlink contains unsupported nested markup");
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

    [[nodiscard]] WorksheetHyperlinkReadSummary finish()
    {
        if (!saw_worksheet_start_ || !saw_sheet_data_start_
            || !saw_sheet_data_end_ || !saw_worksheet_end_) {
            throw FastXlsxError(
                "worksheet hyperlink reader requires a worksheet root and closed sheetData");
        }
        if (!stack_.empty() || active_.has_value()) {
            throw FastXlsxError(
                "worksheet hyperlink reader ended inside an open element");
        }
        audit_hyperlink_overlaps(retained_ranges_);
        return summary_;
    }

private:
    void consume_worksheet_start(const WorksheetEvent& event)
    {
        if (saw_worksheet_start_) {
            throw FastXlsxError(
                "worksheet hyperlink contains duplicate worksheet roots");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "worksheet" || tag.self_closing) {
            throw FastXlsxError(
                "worksheet hyperlink contains an invalid worksheet root");
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
                "worksheet hyperlink worksheet root QName is mismatched");
        }
        saw_worksheet_end_ = true;
    }

    void consume_sheet_data_start(const WorksheetEvent& event)
    {
        if (!saw_worksheet_start_ || saw_sheet_data_start_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet hyperlink contains an invalid sheetData boundary");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "sheetData"
            || tag.prefix != root_prefix_) {
            throw FastXlsxError(
                "worksheet hyperlink sheetData QName is mismatched");
        }
        saw_sheet_data_start_ = true;
    }

    void consume_sheet_data_end(const WorksheetEvent& event)
    {
        if (!saw_sheet_data_start_ || saw_sheet_data_end_) {
            throw FastXlsxError(
                "worksheet hyperlink contains a duplicate sheetData end");
        }
        if (!event.self_closing) {
            const ParsedTag tag = parse_tag(event.raw_xml);
            if (!tag.closing || tag.local_name != "sheetData"
                || tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet hyperlink sheetData QName is mismatched");
            }
        }
        saw_sheet_data_end_ = true;
    }

    void consume_raw_text(std::string_view text)
    {
        if (text.empty() || std::all_of(text.begin(), text.end(), is_xml_space)) {
            return;
        }
        if (inside_target()) {
            throw FastXlsxError(
                "worksheet hyperlink target contains unexpected text");
        }
        if (stack_.empty()) {
            throw FastXlsxError(
                "worksheet hyperlink contains unexpected worksheet text");
        }
    }

    bool inside_target() const noexcept
    {
        return std::any_of(stack_.begin(), stack_.end(), [](const Frame& frame) {
            return frame.role != FrameRole::Generic;
        });
    }

    void consume_metadata(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.local_name != event.element_name) {
            throw FastXlsxError(
                "worksheet hyperlink element local name is mismatched");
        }
        if (tag.closing) {
            consume_metadata_close(tag);
        } else {
            consume_metadata_open(tag);
        }
    }

    void consume_metadata_close(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().local_name != tag.local_name
            || stack_.back().prefix != tag.prefix) {
            throw FastXlsxError(
                "worksheet hyperlink contains mismatched element QName nesting");
        }
        Frame frame = std::move(stack_.back());
        stack_.pop_back();
        restore_namespace_declarations(frame.namespace_changes);
        if (frame.role == FrameRole::Hyperlink) {
            finish_active_hyperlink();
        }
    }

    void enforce_top_level_schema(const ParsedTag& tag)
    {
        if (stack_.empty() && !saw_sheet_data_start_) {
            const std::optional<int> rank = prefix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet hyperlink has unsupported top-level metadata before sheetData");
            }
            if (*rank < last_prefix_rank_) {
                throw FastXlsxError(
                    "worksheet hyperlink prefix elements are not in schema order");
            }
            last_prefix_rank_ = *rank;
            if (tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet hyperlink top-level QName prefix differs from worksheet root");
            }
            return;
        }
        if (stack_.empty() && saw_sheet_data_end_) {
            const std::optional<int> rank = suffix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet hyperlink has unsupported top-level suffix metadata");
            }
            if (*rank < last_suffix_rank_) {
                throw FastXlsxError(
                    "worksheet hyperlink suffix elements are not in schema order");
            }
            last_suffix_rank_ = *rank;
            if (tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet hyperlink top-level QName prefix differs from worksheet root");
            }
            return;
        }
        if (stack_.empty()) {
            throw FastXlsxError(
                "worksheet hyperlink appears in an invalid worksheet region");
        }
    }

    void consume_metadata_open(const ParsedTag& tag)
    {
        enforce_top_level_schema(tag);
        std::vector<NamespaceChange> namespace_changes =
            apply_namespace_declarations(tag);
        if (tag.prefix == root_prefix_ && tag.local_name == "hyperlinks") {
            open_hyperlinks(tag, std::move(namespace_changes));
            return;
        }
        if (tag.prefix == root_prefix_ && tag.local_name == "hyperlink") {
            open_hyperlink(tag, std::move(namespace_changes));
            return;
        }
        if (inside_target()) {
            throw FastXlsxError(
                "worksheet hyperlink contains an unsupported child element");
        }
        open_generic(tag, std::move(namespace_changes));
    }

    void open_hyperlinks(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (!stack_.empty() || saw_hyperlinks_) {
            throw FastXlsxError(
                "worksheet contains duplicate or nested hyperlinks containers");
        }
        if (tag.prefix != root_prefix_) {
            throw FastXlsxError(
                "worksheet hyperlinks QName prefix differs from worksheet root");
        }
        for (const auto& [name, value] : tag.attributes) {
            (void)value;
            if (!is_namespace_declaration(name)) {
                throw FastXlsxError(
                    "worksheet hyperlinks has an unsupported attribute");
            }
        }
        saw_hyperlinks_ = true;
        if (tag.self_closing) {
            restore_namespace_declarations(namespace_changes);
            return;
        }
        push_frame(tag, FrameRole::Hyperlinks, std::move(namespace_changes));
    }

    void open_hyperlink(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (stack_.empty() || stack_.back().role != FrameRole::Hyperlinks) {
            throw FastXlsxError(
                "worksheet hyperlink is not a direct hyperlinks child");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError(
                "worksheet hyperlink QName prefix differs from hyperlinks");
        }
        if (hyperlink_child_count_ >= options_.max_hyperlink_count) {
            throw FastXlsxError(
                "worksheet hyperlink exceeds max_hyperlink_count");
        }

        WorksheetHyperlinkView view;
        view.index = hyperlink_child_count_;
        std::optional<std::string> relationship_id;
        bool saw_reference = false;
        bool saw_location = false;
        bool saw_relationship_id = false;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name = parse_qualified_name(
                name, "attribute name");
            if (attribute_name.prefix.empty() && attribute_name.local_name == "ref") {
                const std::string value = decode_xml_value(raw_value,
                    options_.max_reference_bytes,
                    "ref (max_reference_bytes)");
                summary_.peak_reference_bytes = std::max(
                    summary_.peak_reference_bytes, value.size());
                const std::optional<CellRange> range = parse_a1_range(value);
                if (!range.has_value()) {
                    throw FastXlsxError(
                        "worksheet hyperlink ref is not a valid A1 cell or range");
                }
                view.range = *range;
                saw_reference = true;
            } else if (attribute_name.prefix.empty()
                && attribute_name.local_name == "location") {
                view.location = decode_xml_value(raw_value,
                    options_.max_target_text_bytes,
                    "location (max_target_text_bytes)");
                summary_.peak_target_text_bytes = std::max(
                    summary_.peak_target_text_bytes, view.location.size());
                saw_location = true;
            } else if (attribute_name.prefix.empty()
                && attribute_name.local_name == "display") {
                view.options.display = decode_xml_value(raw_value,
                    options_.max_metadata_text_bytes,
                    "display (max_metadata_text_bytes)");
                summary_.peak_metadata_text_bytes = std::max(
                    summary_.peak_metadata_text_bytes, view.options.display.size());
            } else if (attribute_name.prefix.empty()
                && attribute_name.local_name == "tooltip") {
                view.options.tooltip = decode_xml_value(raw_value,
                    options_.max_metadata_text_bytes,
                    "tooltip (max_metadata_text_bytes)");
                summary_.peak_metadata_text_bytes = std::max(
                    summary_.peak_metadata_text_bytes, view.options.tooltip.size());
            } else if (attribute_name.local_name == "id"
                && !attribute_name.prefix.empty()) {
                if (!is_relationship_prefix(attribute_name.prefix)) {
                    throw FastXlsxError(
                        "worksheet hyperlink relationship id namespace is not the OpenXML relationships namespace");
                }
                if (saw_relationship_id) {
                    throw FastXlsxError(
                        "worksheet hyperlink contains duplicate relationship id attributes");
                }
                relationship_id = decode_xml_value(raw_value,
                    options_.max_relationship_id_bytes,
                    "relationship id (max_relationship_id_bytes)");
                summary_.peak_relationship_id_bytes = std::max(
                    summary_.peak_relationship_id_bytes, relationship_id->size());
                saw_relationship_id = true;
            } else {
                throw FastXlsxError(
                    "worksheet hyperlink has an unsupported attribute");
            }
        }

        if (!saw_reference) {
            throw FastXlsxError("worksheet hyperlink requires ref");
        }
        if (saw_location == relationship_id.has_value()) {
            throw FastXlsxError(
                "worksheet hyperlink requires exactly one of location or relationship id");
        }
        if (saw_location) {
            if (view.location.empty()) {
                throw FastXlsxError(
                    "worksheet internal hyperlink location cannot be empty");
            }
            view.kind = WorksheetHyperlinkKind::Internal;
        } else {
            if (relationship_id->empty()) {
                throw FastXlsxError(
                    "worksheet external hyperlink relationship id cannot be empty");
            }
            view.kind = WorksheetHyperlinkKind::External;
            view.external_target = resolve_external_target(*relationship_id);
            summary_.peak_target_text_bytes = std::max(
                summary_.peak_target_text_bytes, view.external_target.size());
        }

        ++hyperlink_child_count_;
        active_ = std::move(view);
        active_relationship_id_ = relationship_id.value_or(std::string {});
        if (tag.self_closing) {
            finish_active_hyperlink();
            restore_namespace_declarations(namespace_changes);
            return;
        }
        push_frame(tag, FrameRole::Hyperlink, std::move(namespace_changes));
    }

    std::string resolve_external_target(std::string_view relationship_id) const
    {
        if (worksheet_relationships_ == nullptr) {
            throw FastXlsxError(
                "worksheet external hyperlink requires worksheet relationships");
        }
        const Relationship* relationship =
            worksheet_relationships_->find_by_id(relationship_id);
        if (relationship == nullptr) {
            throw FastXlsxError(
                "worksheet external hyperlink relationship id is missing");
        }
        if (relationship->type != hyperlink_relationship_type) {
            throw FastXlsxError(
                "worksheet external hyperlink relationship has the wrong type");
        }
        if (relationship->target_mode != Relationship::TargetMode::External) {
            throw FastXlsxError(
                "worksheet external hyperlink relationship must use TargetMode=External");
        }
        if (relationship->target.empty()) {
            throw FastXlsxError(
                "worksheet external hyperlink relationship target cannot be empty");
        }
        if (relationship->target.size() > options_.max_target_text_bytes) {
            throw FastXlsxError(
                "worksheet external hyperlink target exceeds max_target_text_bytes");
        }
        return relationship->target;
    }

    void open_generic(
        const ParsedTag& tag, std::vector<NamespaceChange> namespace_changes)
    {
        if (tag.self_closing) {
            restore_namespace_declarations(namespace_changes);
            return;
        }
        push_frame(tag, FrameRole::Generic, std::move(namespace_changes));
    }

    void push_frame(const ParsedTag& tag, FrameRole role,
        std::vector<NamespaceChange> namespace_changes)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            throw FastXlsxError(
                "worksheet hyperlink exceeds max_xml_nesting_depth");
        }
        stack_.push_back(Frame {std::string(tag.local_name),
            std::string(tag.prefix), role, std::move(namespace_changes)});
        summary_.peak_xml_nesting_depth = std::max(
            summary_.peak_xml_nesting_depth, stack_.size());
    }

    void finish_active_hyperlink()
    {
        if (!active_.has_value()) {
            throw FastXlsxError(
                "worksheet hyperlink closed without an active value");
        }
        WorksheetHyperlinkView value = std::move(*active_);
        active_.reset();
        retained_ranges_.push_back(value.range);
        summary_.peak_retained_range_count = std::max(
            summary_.peak_retained_range_count, retained_ranges_.size());
        ++summary_.hyperlink_count;
        if (value.kind == WorksheetHyperlinkKind::Internal) {
            ++summary_.internal_hyperlink_count;
        } else {
            ++summary_.external_hyperlink_count;
        }
        if (internal_callbacks_.on_hyperlink) {
            internal_callbacks_.on_hyperlink(value, active_relationship_id_);
        }
        if (callbacks_.on_hyperlink) {
            callbacks_.on_hyperlink(value);
        }
        active_relationship_id_.clear();
    }

    static bool is_namespace_declaration(std::string_view name) noexcept
    {
        return name == "xmlns" || name.starts_with("xmlns:");
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

    std::vector<NamespaceChange> apply_namespace_declarations(
        const ParsedTag& tag)
    {
        std::vector<NamespaceChange> changes;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (!name.starts_with("xmlns:")) {
                continue;
            }
            const std::string_view prefix = name.substr(6U);
            const bool previous = is_relationship_prefix(prefix);
            const std::string uri = decode_xml_value(raw_value,
                options_.max_xml_window_bytes, "namespace URI", false);
            const bool current = uri == relationship_namespace;
            changes.push_back(NamespaceChange {std::string(prefix), previous});
            set_relationship_prefix(prefix, current);
        }
        return changes;
    }

    void restore_namespace_declarations(
        const std::vector<NamespaceChange>& changes)
    {
        for (auto item = changes.rbegin(); item != changes.rend(); ++item) {
            set_relationship_prefix(
                item->prefix, item->was_relationship_namespace);
        }
    }

    const RelationshipSet* worksheet_relationships_ = nullptr;
    const WorksheetHyperlinkReadCallbacks& callbacks_;
    WorksheetHyperlinkReaderOptions options_;
    const WorksheetHyperlinkInternalCallbacks& internal_callbacks_;
    WorksheetHyperlinkReadSummary summary_;
    std::vector<Frame> stack_;
    std::optional<WorksheetHyperlinkView> active_;
    std::string active_relationship_id_;
    std::vector<CellRange> retained_ranges_;
    std::vector<std::string> relationship_prefixes_;
    std::string root_prefix_;
    std::uint64_t hyperlink_child_count_ = 0;
    int last_prefix_rank_ = 0;
    int last_suffix_rank_ = 0;
    bool saw_worksheet_start_ = false;
    bool saw_sheet_data_start_ = false;
    bool saw_sheet_data_end_ = false;
    bool saw_worksheet_end_ = false;
    bool saw_hyperlinks_ = false;
    bool inside_cell_ = false;
};

} // namespace

WorksheetHyperlinkReadSummary read_worksheet_hyperlinks_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const RelationshipSet* worksheet_relationships,
    const WorksheetHyperlinkReadCallbacks& callbacks,
    WorksheetHyperlinkReaderOptions options,
    const WorksheetHyperlinkInternalCallbacks& internal_callbacks)
{
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError(
            "WorksheetHyperlinkReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError(
            "WorksheetHyperlinkReader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_hyperlink_count == 0) {
        throw FastXlsxError(
            "WorksheetHyperlinkReader requires nonzero max_hyperlink_count");
    }
    if (options.max_reference_bytes == 0) {
        throw FastXlsxError(
            "WorksheetHyperlinkReader requires nonzero max_reference_bytes");
    }
    if (options.max_relationship_id_bytes == 0) {
        throw FastXlsxError(
            "WorksheetHyperlinkReader requires nonzero max_relationship_id_bytes");
    }
    if (options.max_target_text_bytes == 0) {
        throw FastXlsxError(
            "WorksheetHyperlinkReader requires nonzero max_target_text_bytes");
    }
    if (options.max_metadata_text_bytes == 0) {
        throw FastXlsxError(
            "WorksheetHyperlinkReader requires nonzero max_metadata_text_bytes");
    }

    WorksheetHyperlinkProjectionReader projection(
        worksheet_relationships, callbacks, options, internal_callbacks);
    WorksheetEventReaderOptions event_options;
    event_options.max_window_bytes = options.max_xml_window_bytes;
    event_options.copy_context_attributes = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&projection](const WorksheetEvent& event) { projection.consume(event); },
        event_options);
    return projection.finish();
}

} // namespace fastxlsx::detail
