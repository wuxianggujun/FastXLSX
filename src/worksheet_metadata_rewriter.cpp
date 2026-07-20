#include <fastxlsx/detail/worksheet_metadata_rewriter.hpp>

#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr int auto_filter_schema_rank = 5;
constexpr int merged_cells_schema_rank = 9;
constexpr int data_validations_schema_rank = 12;
constexpr int hyperlink_schema_rank = 13;
constexpr std::uint32_t max_freeze_pane_row_split = 1048575U;
constexpr std::uint32_t max_freeze_pane_column_split = 16383U;

bool is_xml_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool is_closing_tag(std::string_view raw_xml) noexcept
{
    return raw_xml.size() > 2 && raw_xml[0] == '<' && raw_xml[1] == '/';
}

std::optional<std::string_view> attribute_value(
    std::string_view raw_tag, std::string_view requested_name)
{
    if (raw_tag.size() < 3 || raw_tag.front() != '<' || raw_tag.back() != '>') {
        throw FastXlsxError("worksheet metadata contains an invalid XML tag");
    }

    std::size_t position = 1;
    const bool closing_tag = position < raw_tag.size() && raw_tag[position] == '/';
    if (closing_tag) {
        ++position;
    }
    const std::size_t element_name_begin = position;
    while (position < raw_tag.size() && !is_xml_space(raw_tag[position])
        && raw_tag[position] != '/' && raw_tag[position] != '>') {
        ++position;
    }
    if (position == element_name_begin) {
        throw FastXlsxError("worksheet metadata contains an empty element name");
    }

    std::optional<std::string_view> requested_value;
    std::unordered_set<std::string_view> attribute_names;

    while (position < raw_tag.size()) {
        while (position < raw_tag.size() && is_xml_space(raw_tag[position])) {
            ++position;
        }
        if (position >= raw_tag.size()) {
            break;
        }
        if (raw_tag[position] == '>') {
            if (position + 1 != raw_tag.size()) {
                throw FastXlsxError(
                    "worksheet metadata contains trailing bytes after an XML tag");
            }
            ++position;
            break;
        }
        if (raw_tag[position] == '/') {
            if (closing_tag || position + 2 != raw_tag.size()
                || raw_tag[position + 1] != '>') {
                throw FastXlsxError(
                    "worksheet metadata contains an invalid self-closing tag tail");
            }
            position += 2;
            break;
        }
        if (closing_tag) {
            throw FastXlsxError(
                "worksheet metadata closing tag contains attributes");
        }

        const std::size_t name_begin = position;
        while (position < raw_tag.size() && !is_xml_space(raw_tag[position])
            && raw_tag[position] != '=' && raw_tag[position] != '/'
            && raw_tag[position] != '>') {
            ++position;
        }
        const std::string_view name = raw_tag.substr(name_begin, position - name_begin);
        if (name.empty()) {
            throw FastXlsxError("worksheet metadata contains an empty attribute name");
        }
        while (position < raw_tag.size() && is_xml_space(raw_tag[position])) {
            ++position;
        }
        if (position >= raw_tag.size() || raw_tag[position] != '=') {
            throw FastXlsxError(
                "worksheet metadata contains an attribute without a value");
        }
        ++position;
        while (position < raw_tag.size() && is_xml_space(raw_tag[position])) {
            ++position;
        }
        if (position >= raw_tag.size()
            || (raw_tag[position] != '"' && raw_tag[position] != '\'')) {
            throw FastXlsxError(
                "worksheet metadata contains an unquoted attribute value");
        }

        const char quote = raw_tag[position++];
        const std::size_t value_begin = position;
        while (position < raw_tag.size() && raw_tag[position] != quote) {
            ++position;
        }
        if (position >= raw_tag.size()) {
            throw FastXlsxError(
                "worksheet metadata contains an unterminated attribute value");
        }
        const std::string_view value =
            raw_tag.substr(value_begin, position - value_begin);
        ++position;
        if (position < raw_tag.size() && !is_xml_space(raw_tag[position])
            && raw_tag[position] != '/' && raw_tag[position] != '>') {
            throw FastXlsxError(
                "worksheet metadata attributes are not separated by whitespace");
        }
        if (!attribute_names.emplace(name).second) {
            throw FastXlsxError(
                "worksheet metadata contains a duplicate attribute");
        }
        if (name == requested_name) {
            requested_value = value;
        }
    }

    if (position != raw_tag.size()) {
        throw FastXlsxError("worksheet metadata contains an incomplete XML tag");
    }
    return requested_value;
}

std::string xml_element_prefix(
    std::string_view raw_tag, std::string_view expected_local_name)
{
    (void)attribute_value(raw_tag, {});

    std::size_t name_begin = is_closing_tag(raw_tag) ? 2U : 1U;
    std::size_t name_end = name_begin;
    while (name_end < raw_tag.size() && !is_xml_space(raw_tag[name_end])
        && raw_tag[name_end] != '/' && raw_tag[name_end] != '>') {
        ++name_end;
    }
    const std::string_view qualified_name =
        raw_tag.substr(name_begin, name_end - name_begin);
    const std::size_t separator = qualified_name.find(':');
    if (separator == 0 || separator + 1 == qualified_name.size()
        || (separator != std::string_view::npos
            && qualified_name.find(':', separator + 1) != std::string_view::npos)) {
        throw FastXlsxError("worksheet metadata contains an invalid qualified element name");
    }
    const std::string_view local_name = separator == std::string_view::npos
        ? qualified_name
        : qualified_name.substr(separator + 1);
    if (local_name != expected_local_name) {
        throw FastXlsxError(
            "worksheet metadata element QName does not match its parsed local name");
    }
    return separator == std::string_view::npos
        ? std::string {}
        : std::string(qualified_name.substr(0, separator + 1));
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

    std::uint32_t column = 0;
    const std::size_t column_begin = position;
    while (position < text.size()
        && std::isalpha(static_cast<unsigned char>(text[position])) != 0) {
        const char upper = static_cast<char>(
            std::toupper(static_cast<unsigned char>(text[position])));
        if (upper < 'A' || upper > 'Z'
            || column > (16384U - static_cast<std::uint32_t>(upper - 'A' + 1)) / 26U) {
            return std::nullopt;
        }
        column = column * 26U + static_cast<std::uint32_t>(upper - 'A' + 1);
        ++position;
    }
    if (position == column_begin || column == 0 || column > 16384U) {
        return std::nullopt;
    }
    if (position < text.size() && text[position] == '$') {
        ++position;
    }

    std::uint32_t row = 0;
    const std::size_t row_begin = position;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
        const std::uint32_t digit = static_cast<std::uint32_t>(text[position] - '0');
        if (row > (1048576U - digit) / 10U) {
            return std::nullopt;
        }
        row = row * 10U + digit;
        ++position;
    }
    if (position == row_begin || position != text.size() || row == 0 || row > 1048576U) {
        return std::nullopt;
    }
    return A1Coordinate {row, column};
}

struct A1Range {
    A1Coordinate first;
    A1Coordinate last;
};

struct ExistingMergedCell {
    A1Range range;
    std::uint64_t source_offset = 0;
    std::uint64_t source_end_offset = 0;
};

std::optional<A1Range> parse_a1_range(std::string_view reference)
{
    const std::size_t separator = reference.find(':');
    if (separator != std::string_view::npos
        && reference.find(':', separator + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    const std::optional<A1Coordinate> first = parse_a1_coordinate(
        separator == std::string_view::npos ? reference : reference.substr(0, separator));
    const std::optional<A1Coordinate> last = separator == std::string_view::npos
        ? first
        : parse_a1_coordinate(reference.substr(separator + 1));
    if (!first.has_value() || !last.has_value()
        || first->row > last->row || first->column > last->column) {
        return std::nullopt;
    }
    return A1Range {*first, *last};
}

A1Range a1_range_from_cell_range(CellRange range) noexcept
{
    return A1Range {
        {range.first_row, range.first_column},
        {range.last_row, range.last_column},
    };
}

bool a1_ranges_equal(const A1Range& left, const A1Range& right) noexcept
{
    return left.first.row == right.first.row
        && left.first.column == right.first.column
        && left.last.row == right.last.row
        && left.last.column == right.last.column;
}

bool a1_ranges_overlap(const A1Range& left, const A1Range& right) noexcept
{
    return left.first.row <= right.last.row && right.first.row <= left.last.row
        && left.first.column <= right.last.column
        && right.first.column <= left.last.column;
}

bool a1_range_is_single_cell(const A1Range& range) noexcept
{
    return range.first.row == range.last.row
        && range.first.column == range.last.column;
}

void audit_existing_merged_cell_overlaps(
    const std::vector<ExistingMergedCell>& existing_cells)
{
    std::vector<std::size_t> ordered_indices(existing_cells.size());
    std::iota(ordered_indices.begin(), ordered_indices.end(), std::size_t {0});
    std::sort(ordered_indices.begin(), ordered_indices.end(),
        [&existing_cells](std::size_t left_index, std::size_t right_index) {
            const A1Range& left = existing_cells[left_index].range;
            const A1Range& right = existing_cells[right_index].range;
            if (left.first.row != right.first.row) {
                return left.first.row < right.first.row;
            }
            if (left.first.column != right.first.column) {
                return left.first.column < right.first.column;
            }
            if (left.last.row != right.last.row) {
                return left.last.row < right.last.row;
            }
            return left.last.column < right.last.column;
        });

    std::multimap<std::uint32_t, std::size_t> active_by_last_row;
    std::map<std::uint32_t, std::size_t> active_by_first_column;
    for (const std::size_t current_index : ordered_indices) {
        const A1Range& current = existing_cells[current_index].range;
        while (!active_by_last_row.empty()
            && active_by_last_row.begin()->first < current.first.row) {
            const std::size_t expired_index = active_by_last_row.begin()->second;
            const auto active_column = active_by_first_column.find(
                existing_cells[expired_index].range.first.column);
            if (active_column == active_by_first_column.end()
                || active_column->second != expired_index) {
                throw FastXlsxError(
                    "worksheet merged-cell overlap audit lost its active range");
            }
            active_by_first_column.erase(active_column);
            active_by_last_row.erase(active_by_last_row.begin());
        }

        const auto next = active_by_first_column.lower_bound(current.first.column);
        if (next != active_by_first_column.end()
            && existing_cells[next->second].range.first.column <= current.last.column) {
            throw FastXlsxError(
                "existing worksheet merged-cell ranges overlap or duplicate");
        }
        if (next != active_by_first_column.begin()) {
            const auto previous = std::prev(next);
            if (existing_cells[previous->second].range.last.column
                >= current.first.column) {
                throw FastXlsxError(
                    "existing worksheet merged-cell ranges overlap or duplicate");
            }
        }

        active_by_first_column.emplace(current.first.column, current_index);
        active_by_last_row.emplace(current.last.row, current_index);
    }
}

bool hyperlink_ref_contains_target(
    std::string_view reference, const A1Coordinate& target)
{
    const std::optional<A1Range> range = parse_a1_range(reference);
    if (!range.has_value()) {
        throw FastXlsxError("existing worksheet hyperlink ref is not a valid A1 range");
    }
    return target.row >= range->first.row && target.row <= range->last.row
        && target.column >= range->first.column
        && target.column <= range->last.column;
}

std::uint64_t event_end_offset(const WorksheetEvent& event)
{
    if (event.raw_xml.size()
        > std::numeric_limits<std::uint64_t>::max() - event.raw_xml_offset) {
        throw FastXlsxError("worksheet metadata event offset exceeds supported range");
    }
    return event.raw_xml_offset + static_cast<std::uint64_t>(event.raw_xml.size());
}

std::optional<int> worksheet_suffix_schema_rank(std::string_view element_name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"sheetCalcPr", 1},
        {"sheetProtection", 2},
        {"protectedRanges", 3},
        {"scenarios", 4},
        {"autoFilter", 5},
        {"sortState", 6},
        {"dataConsolidate", 7},
        {"customSheetViews", 8},
        {"mergeCells", 9},
        {"phoneticPr", 10},
        {"conditionalFormatting", 11},
        {"dataValidations", 12},
        {"hyperlinks", hyperlink_schema_rank},
        {"printOptions", 14},
        {"pageMargins", 15},
        {"pageSetup", 16},
        {"headerFooter", 17},
        {"rowBreaks", 18},
        {"colBreaks", 19},
        {"customProperties", 20},
        {"cellWatches", 21},
        {"ignoredErrors", 22},
        {"smartTags", 23},
        {"drawing", 24},
        {"legacyDrawing", 25},
        {"legacyDrawingHF", 26},
        {"picture", 27},
        {"oleObjects", 28},
        {"controls", 29},
        {"webPublishItems", 30},
        {"tableParts", 31},
        {"extLst", 32},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [element_name](const auto& entry) { return entry.first == element_name; });
    if (found == std::end(ranks)) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<int> worksheet_prefix_schema_rank(std::string_view element_name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"sheetPr", 1},
        {"dimension", 2},
        {"sheetViews", 3},
        {"sheetFormatPr", 4},
        {"cols", 5},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [element_name](const auto& entry) { return entry.first == element_name; });
    if (found == std::end(ranks)) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<int> sheet_view_child_schema_rank(std::string_view element_name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"pane", 1},
        {"selection", 2},
        {"pivotSelection", 3},
        {"extLst", 4},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [element_name](const auto& entry) { return entry.first == element_name; });
    if (found == std::end(ranks)) {
        return std::nullopt;
    }
    return found->second;
}

std::string internal_hyperlink_xml(const WorksheetInternalHyperlinkRewrite& hyperlink)
{
    std::string xml = "<hyperlink ref=\"";
    append_escaped_xml_attribute(xml, hyperlink.cell_reference);
    xml += "\" location=\"";
    append_escaped_xml_attribute(xml, hyperlink.location);
    xml += '"';
    if (!hyperlink.display.empty()) {
        xml += " display=\"";
        append_escaped_xml_attribute(xml, hyperlink.display);
        xml += '"';
    }
    if (!hyperlink.tooltip.empty()) {
        xml += " tooltip=\"";
        append_escaped_xml_attribute(xml, hyperlink.tooltip);
        xml += '"';
    }
    xml += "/>";
    return xml;
}

std::string external_hyperlink_xml(const WorksheetExternalHyperlinkRewrite& hyperlink)
{
    std::string xml = "<hyperlink ref=\"";
    append_escaped_xml_attribute(xml, hyperlink.cell_reference);
    xml += "\" r:id=\"";
    append_escaped_xml_attribute(xml, hyperlink.relationship_id);
    xml += '"';
    if (!hyperlink.display.empty()) {
        xml += " display=\"";
        append_escaped_xml_attribute(xml, hyperlink.display);
        xml += '"';
    }
    if (!hyperlink.tooltip.empty()) {
        xml += " tooltip=\"";
        append_escaped_xml_attribute(xml, hyperlink.tooltip);
        xml += '"';
    }
    xml += "/>";
    return xml;
}

std::string_view data_validation_type_name(DataValidationType type)
{
    switch (type) {
    case DataValidationType::Whole:
        return "whole";
    case DataValidationType::Decimal:
        return "decimal";
    case DataValidationType::List:
        return "list";
    case DataValidationType::Date:
        return "date";
    case DataValidationType::Time:
        return "time";
    case DataValidationType::TextLength:
        return "textLength";
    case DataValidationType::Custom:
        return "custom";
    }
    throw FastXlsxError("unknown data validation type");
}

std::string_view data_validation_operator_name(DataValidationOperator operator_type)
{
    switch (operator_type) {
    case DataValidationOperator::Between:
        return "between";
    case DataValidationOperator::NotBetween:
        return "notBetween";
    case DataValidationOperator::Equal:
        return "equal";
    case DataValidationOperator::NotEqual:
        return "notEqual";
    case DataValidationOperator::GreaterThan:
        return "greaterThan";
    case DataValidationOperator::LessThan:
        return "lessThan";
    case DataValidationOperator::GreaterThanOrEqual:
        return "greaterThanOrEqual";
    case DataValidationOperator::LessThanOrEqual:
        return "lessThanOrEqual";
    }
    throw FastXlsxError("unknown data validation operator");
}

std::string_view data_validation_error_style_name(DataValidationErrorStyle error_style)
{
    switch (error_style) {
    case DataValidationErrorStyle::Stop:
        return "stop";
    case DataValidationErrorStyle::Warning:
        return "warning";
    case DataValidationErrorStyle::Information:
        return "information";
    }
    throw FastXlsxError("unknown data validation error style");
}

bool data_validation_operator_requires_formula2(DataValidationOperator operator_type) noexcept
{
    return operator_type == DataValidationOperator::Between
        || operator_type == DataValidationOperator::NotBetween;
}

std::optional<std::uint64_t> parse_unsigned_decimal(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return std::nullopt;
        }
        parsed = parsed * 10U + digit;
    }
    return parsed;
}

void audit_existing_frozen_pane(std::string_view raw_tag)
{
    const std::optional<std::string_view> state = attribute_value(raw_tag, "state");
    if (!state.has_value() || *state != "frozen") {
        throw FastXlsxError(
            "existing worksheet pane is not a supported frozen pane");
    }

    std::uint32_t row_split = 0;
    std::uint32_t column_split = 0;
    if (const std::optional<std::string_view> x_split =
            attribute_value(raw_tag, "xSplit")) {
        const std::optional<std::uint64_t> parsed = parse_unsigned_decimal(*x_split);
        if (!parsed.has_value() || *parsed > max_freeze_pane_column_split) {
            throw FastXlsxError(
                "existing worksheet frozen pane has an invalid xSplit");
        }
        column_split = static_cast<std::uint32_t>(*parsed);
    }
    if (const std::optional<std::string_view> y_split =
            attribute_value(raw_tag, "ySplit")) {
        const std::optional<std::uint64_t> parsed = parse_unsigned_decimal(*y_split);
        if (!parsed.has_value() || *parsed > max_freeze_pane_row_split) {
            throw FastXlsxError(
                "existing worksheet frozen pane has an invalid ySplit");
        }
        row_split = static_cast<std::uint32_t>(*parsed);
    }
    if (row_split == 0 && column_split == 0) {
        throw FastXlsxError(
            "existing worksheet frozen pane has no non-zero split");
    }
    if (const std::optional<std::string_view> top_left =
            attribute_value(raw_tag, "topLeftCell")) {
        if (!parse_a1_coordinate(*top_left).has_value()) {
            throw FastXlsxError(
                "existing worksheet frozen pane has an invalid topLeftCell");
        }
    }
}

bool selection_pane_is_preservable(
    std::string_view pane,
    std::uint32_t row_split,
    std::uint32_t column_split,
    WorksheetFreezePaneRewriteOperation operation)
{
    if (operation == WorksheetFreezePaneRewriteOperation::Clear) {
        return false;
    }
    if (pane == "topLeft") {
        return true;
    }
    if (pane == "topRight") {
        return column_split != 0;
    }
    if (pane == "bottomLeft") {
        return row_split != 0;
    }
    if (pane == "bottomRight") {
        return row_split != 0 && column_split != 0;
    }
    return false;
}

std::string metadata_container_opening_with_count(
    std::string_view raw_tag,
    std::uint64_t count,
    bool expand_self_closing,
    std::string_view container_label)
{
    std::string tag(raw_tag);
    if (expand_self_closing) {
        std::size_t slash = tag.size() - 2;
        while (slash > 0 && is_xml_space(tag[slash])) {
            --slash;
        }
        if (tag[slash] != '/') {
            throw FastXlsxError("worksheet " + std::string(container_label)
                + " rewrite expected a self-closing container");
        }
        tag.erase(slash, 1);
    }

    const std::optional<std::string_view> current = attribute_value(tag, "count");
    std::string count_text;
    append_unsigned_decimal(count_text, count);
    if (current.has_value()) {
        const std::size_t value_offset = static_cast<std::size_t>(
            current->data() - tag.data());
        tag.replace(value_offset, current->size(), count_text);
    } else {
        if (tag.size() < 2 || tag.back() != '>') {
            throw FastXlsxError("worksheet " + std::string(container_label)
                + " container has an invalid opening tag");
        }
        tag.insert(tag.size() - 1, " count=\"" + count_text + "\"");
    }
    return tag;
}

std::string worksheet_root_with_relationship_namespace(std::string_view raw_tag)
{
    constexpr std::string_view relationship_namespace =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    const std::optional<std::string_view> current = attribute_value(raw_tag, "xmlns:r");
    if (current.has_value()) {
        if (*current != relationship_namespace) {
            throw FastXlsxError(
                "worksheet r namespace is not the OpenXML relationships namespace");
        }
        return std::string(raw_tag);
    }
    if (raw_tag.size() < 3 || raw_tag.front() != '<' || raw_tag.back() != '>'
        || is_closing_tag(raw_tag)) {
        throw FastXlsxError(
            "worksheet external hyperlink edit requires a valid worksheet root tag");
    }

    std::string root(raw_tag);
    root.insert(root.size() - 1,
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");
    return root;
}

bool is_synthetic_self_closing_end(const WorksheetEvent& event) noexcept
{
    if (!event.self_closing) {
        return false;
    }
    return event.kind == WorksheetEventKind::WorksheetEnd
        || event.kind == WorksheetEventKind::SheetDataEnd
        || event.kind == WorksheetEventKind::RowEnd
        || event.kind == WorksheetEventKind::CellEnd;
}

void write_bytes(std::ofstream& output, std::string_view bytes)
{
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw FastXlsxError("failed to write staged worksheet metadata");
    }
}

std::string expand_self_closing_metadata_tag(
    std::string_view raw_tag, std::string_view metadata_label)
{
    std::string opening(raw_tag);
    std::size_t slash = opening.size() - 2;
    while (slash > 0 && is_xml_space(opening[slash])) {
        --slash;
    }
    if (opening[slash] != '/') {
        throw FastXlsxError("worksheet " + std::string(metadata_label)
            + " rewrite expected a self-closing tag");
    }
    opening.erase(slash, 1);
    return opening;
}

std::string expand_self_closing_tag(std::string_view raw_tag)
{
    return expand_self_closing_metadata_tag(raw_tag, "hyperlink");
}

} // namespace

std::string serialize_worksheet_auto_filter(CellRange range)
{
    std::string xml = "<autoFilter ref=\"";
    xml += range_reference(range);
    xml += "\"/>";
    return xml;
}

void validate_freeze_pane_split(
    std::uint32_t row_split, std::uint32_t column_split)
{
    if (row_split > max_freeze_pane_row_split
        || column_split > max_freeze_pane_column_split) {
        throw FastXlsxError("invalid freeze pane split");
    }
}

std::string serialize_worksheet_frozen_pane(
    std::uint32_t row_split,
    std::uint32_t column_split,
    std::string_view element_prefix)
{
    validate_freeze_pane_split(row_split, column_split);
    if (row_split == 0 && column_split == 0) {
        return {};
    }

    std::string xml = "<";
    xml += element_prefix;
    xml += "pane";
    if (column_split != 0) {
        xml += " xSplit=\"";
        append_unsigned_decimal(xml, column_split);
        xml += "\"";
    }
    if (row_split != 0) {
        xml += " ySplit=\"";
        append_unsigned_decimal(xml, row_split);
        xml += "\"";
    }
    xml += " topLeftCell=\"";
    append_cell_reference(xml, row_split + 1U, column_split + 1U);
    xml += "\" activePane=\"";
    if (row_split != 0 && column_split != 0) {
        xml += "bottomRight";
    } else if (row_split != 0) {
        xml += "bottomLeft";
    } else {
        xml += "topRight";
    }
    xml += "\" state=\"frozen\"/>";
    return xml;
}

void validate_merged_cell_range(CellRange range)
{
    (void)range_reference(range);
    if (range.first_row == range.last_row
        && range.first_column == range.last_column) {
        throw FastXlsxError("merged range must include more than one cell");
    }
}

std::string serialize_worksheet_merged_cell(
    CellRange range, std::string_view element_prefix)
{
    validate_merged_cell_range(range);
    std::string xml = "<";
    xml += element_prefix;
    xml += "mergeCell ref=\"";
    xml += range_reference(range);
    xml += "\"/>";
    return xml;
}

void validate_data_validation_rule(const DataValidationRule& rule)
{
    if (rule.formula1.empty()) {
        throw FastXlsxError("data validation formula1 cannot be empty");
    }

    if (rule.hide_dropdown_arrow && rule.type != DataValidationType::List) {
        throw FastXlsxError("hide_dropdown_arrow is only valid for list data validations");
    }

    if (rule.type == DataValidationType::List || rule.type == DataValidationType::Custom) {
        if (rule.operator_type.has_value()) {
            throw FastXlsxError("list and custom data validations do not accept an operator");
        }
        if (!rule.formula2.empty()) {
            throw FastXlsxError("list and custom data validations do not accept formula2");
        }
        return;
    }

    if (!rule.operator_type.has_value()) {
        throw FastXlsxError("data validation operator is required for this type");
    }
    if (data_validation_operator_requires_formula2(*rule.operator_type)) {
        if (rule.formula2.empty()) {
            throw FastXlsxError("between data validations require formula2");
        }
    } else if (!rule.formula2.empty()) {
        throw FastXlsxError("single-formula data validation operator cannot use formula2");
    }
}

std::string serialize_data_validation(
    std::span<const CellRange> ranges, const DataValidationRule& rule)
{
    validate_data_validation_rule(rule);
    const std::string range_text = sqref(ranges);

    std::string xml = "<dataValidation type=\"";
    xml += data_validation_type_name(rule.type);
    xml += '"';
    if (rule.allow_blank) {
        xml += " allowBlank=\"1\"";
    }
    if (rule.hide_dropdown_arrow) {
        xml += " showDropDown=\"1\"";
    }
    if (rule.show_input_message) {
        xml += " showInputMessage=\"1\"";
    }
    if (rule.show_error_message) {
        xml += " showErrorMessage=\"1\"";
    }
    if (rule.error_style.has_value()) {
        xml += " errorStyle=\"";
        xml += data_validation_error_style_name(*rule.error_style);
        xml += '"';
    }
    if (!rule.error_title.empty()) {
        xml += " errorTitle=\"";
        append_escaped_xml_attribute(xml, rule.error_title);
        xml += '"';
    }
    if (!rule.error.empty()) {
        xml += " error=\"";
        append_escaped_xml_attribute(xml, rule.error);
        xml += '"';
    }
    if (!rule.prompt_title.empty()) {
        xml += " promptTitle=\"";
        append_escaped_xml_attribute(xml, rule.prompt_title);
        xml += '"';
    }
    if (!rule.prompt.empty()) {
        xml += " prompt=\"";
        append_escaped_xml_attribute(xml, rule.prompt);
        xml += '"';
    }
    if (rule.operator_type.has_value()) {
        xml += " operator=\"";
        xml += data_validation_operator_name(*rule.operator_type);
        xml += '"';
    }
    xml += " sqref=\"";
    append_escaped_xml_attribute(xml, range_text);
    xml += "\"><formula1>";
    append_escaped_xml_text(xml, rule.formula1);
    xml += "</formula1>";
    if (!rule.formula2.empty()) {
        xml += "<formula2>";
        append_escaped_xml_text(xml, rule.formula2);
        xml += "</formula2>";
    }
    xml += "</dataValidation>";
    return xml;
}

WorksheetInternalHyperlinkRewritePlan plan_worksheet_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view cell_reference)
{
    const std::optional<A1Coordinate> target = parse_a1_coordinate(cell_reference);
    if (!target.has_value()) {
        throw FastXlsxError("hyperlink cell reference is invalid");
    }

    bool saw_sheet_data_end = false;
    bool saw_worksheet_end = false;
    bool saw_hyperlinks = false;
    int last_suffix_rank = 0;
    std::vector<std::string> metadata_stack;
    std::optional<std::uint64_t> first_after_hyperlinks_offset;
    std::optional<WorksheetInternalHyperlinkRewritePlan> existing_container_plan;
    std::uint64_t worksheet_end_offset = 0;

    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::SheetDataEnd) {
                saw_sheet_data_end = true;
                return;
            }
            if (event.kind == WorksheetEventKind::WorksheetEnd) {
                if (!event.self_closing) {
                    saw_worksheet_end = true;
                    worksheet_end_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind != WorksheetEventKind::Metadata) {
                return;
            }

            const bool closing = is_closing_tag(event.raw_xml);
            if (closing) {
                if (metadata_stack.empty() || metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "worksheet hyperlink metadata contains mismatched element nesting");
                }
                if (metadata_stack.size() == 1 && event.element_name == "hyperlinks") {
                    if (!saw_hyperlinks || existing_container_plan.has_value()) {
                        throw FastXlsxError(
                            "worksheet contains duplicate or ambiguous hyperlinks metadata");
                    }
                    existing_container_plan = WorksheetInternalHyperlinkRewritePlan {
                        WorksheetInternalHyperlinkRewriteAction::AppendBeforeContainerClose,
                        event.raw_xml_offset,
                    };
                }
                metadata_stack.pop_back();
                return;
            }

            const bool top_level = metadata_stack.empty();
            const bool direct_hyperlink_child = metadata_stack.size() == 1
                && metadata_stack.front() == "hyperlinks";
            if (top_level && saw_sheet_data_end) {
                const std::optional<int> rank =
                    worksheet_suffix_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet contains top-level suffix metadata whose position relative "
                        "to hyperlinks is unsupported");
                }
                if (*rank < last_suffix_rank) {
                    throw FastXlsxError(
                        "worksheet top-level suffix metadata is not in schema order");
                }
                last_suffix_rank = *rank;
                if (*rank > hyperlink_schema_rank
                    && !first_after_hyperlinks_offset.has_value()) {
                    first_after_hyperlinks_offset = event.raw_xml_offset;
                }
                if (event.element_name == "hyperlinks") {
                    if (saw_hyperlinks) {
                        throw FastXlsxError("worksheet contains duplicate hyperlinks containers");
                    }
                    saw_hyperlinks = true;
                    if (event.self_closing) {
                        existing_container_plan = WorksheetInternalHyperlinkRewritePlan {
                            WorksheetInternalHyperlinkRewriteAction::
                                ExpandSelfClosingContainer,
                            event.raw_xml_offset,
                        };
                    }
                }
            } else if (top_level && event.element_name == "hyperlinks") {
                throw FastXlsxError(
                    "worksheet hyperlinks metadata appears before sheetData");
            }

            if (direct_hyperlink_child) {
                if (event.element_name != "hyperlink") {
                    throw FastXlsxError(
                        "worksheet hyperlinks container has an unsupported child element");
                }
                const std::optional<std::string_view> reference =
                    attribute_value(event.raw_xml, "ref");
                if (!reference.has_value() || reference->empty()) {
                    throw FastXlsxError("existing worksheet hyperlink is missing its ref");
                }
                if (hyperlink_ref_contains_target(*reference, *target)) {
                    throw FastXlsxError(
                        "worksheet already has a hyperlink covering the requested cell");
                }
            }

            if (!event.self_closing) {
                metadata_stack.emplace_back(event.element_name);
            }
        });

    if (!metadata_stack.empty()) {
        throw FastXlsxError("worksheet hyperlink metadata ended inside an open element");
    }
    if (!saw_sheet_data_end || !saw_worksheet_end) {
        throw FastXlsxError(
            "worksheet hyperlink edit requires sheetData and a closing worksheet root");
    }
    if (saw_hyperlinks) {
        if (!existing_container_plan.has_value()) {
            throw FastXlsxError("worksheet hyperlinks container has no closing boundary");
        }
        return *existing_container_plan;
    }
    return WorksheetInternalHyperlinkRewritePlan {
        WorksheetInternalHyperlinkRewriteAction::InsertContainerBefore,
        first_after_hyperlinks_offset.value_or(worksheet_end_offset),
    };
}

WorksheetInternalHyperlinkRewritePlan plan_worksheet_internal_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetInternalHyperlinkRewrite& hyperlink)
{
    if (hyperlink.location.empty()) {
        throw FastXlsxError("internal hyperlink location cannot be empty");
    }
    return plan_worksheet_hyperlink_rewrite(
        read_next_chunk, hyperlink.cell_reference);
}

WorksheetInternalHyperlinkRewritePlan plan_worksheet_external_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetExternalHyperlinkRewrite& hyperlink)
{
    if (hyperlink.target.empty()) {
        throw FastXlsxError("external hyperlink target cannot be empty");
    }
    if (hyperlink.relationship_id.empty()) {
        throw FastXlsxError("external hyperlink relationship id cannot be empty");
    }
    return plan_worksheet_hyperlink_rewrite(
        read_next_chunk, hyperlink.cell_reference);
}

WorksheetDataValidationRewritePlan plan_worksheet_data_validation_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk)
{
    bool saw_sheet_data_end = false;
    bool saw_worksheet_end = false;
    bool saw_data_validations = false;
    std::size_t child_count = 0;
    std::optional<std::uint64_t> declared_count;
    std::optional<std::uint64_t> first_after_data_validations_offset;
    std::uint64_t worksheet_end_offset = 0;
    std::uint64_t container_start_offset = 0;
    int last_suffix_rank = 0;
    std::vector<std::string> metadata_stack;
    std::optional<WorksheetDataValidationRewritePlan> plan;

    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::SheetDataEnd) {
                saw_sheet_data_end = true;
                return;
            }
            if (event.kind == WorksheetEventKind::WorksheetEnd) {
                if (!event.self_closing) {
                    saw_worksheet_end = true;
                    worksheet_end_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind != WorksheetEventKind::Metadata) {
                return;
            }

            const bool closing = is_closing_tag(event.raw_xml);
            if (closing) {
                if (metadata_stack.empty() || metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "worksheet data validation metadata contains mismatched element nesting");
                }
                if (metadata_stack.size() == 1 && event.element_name == "dataValidations") {
                    if (!saw_data_validations || plan.has_value()) {
                        throw FastXlsxError(
                            "worksheet contains duplicate or ambiguous data validation metadata");
                    }
                    if (declared_count.has_value()
                        && *declared_count != static_cast<std::uint64_t>(child_count)) {
                        throw FastXlsxError(
                            "worksheet data validation count does not match its direct children");
                    }
                    plan = WorksheetDataValidationRewritePlan {
                        WorksheetDataValidationRewritePlan::Action::AppendBeforeContainerClose,
                        event.raw_xml_offset,
                        container_start_offset,
                        static_cast<std::uint64_t>(child_count) + 1U};
                }
                metadata_stack.pop_back();
                return;
            }

            const bool top_level = metadata_stack.empty();
            const bool direct_data_validation_child = metadata_stack.size() == 1
                && metadata_stack.front() == "dataValidations";
            if (top_level && saw_sheet_data_end) {
                const std::optional<int> rank =
                    worksheet_suffix_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet contains top-level suffix metadata whose position relative "
                        "to data validations is unsupported");
                }
                if (*rank < last_suffix_rank) {
                    throw FastXlsxError(
                        "worksheet top-level suffix metadata is not in schema order");
                }
                last_suffix_rank = *rank;
                if (*rank > data_validations_schema_rank
                    && !first_after_data_validations_offset.has_value()) {
                    first_after_data_validations_offset = event.raw_xml_offset;
                }
                if (event.element_name == "dataValidations") {
                    if (saw_data_validations) {
                        throw FastXlsxError(
                            "worksheet contains duplicate data validation containers");
                    }
                    saw_data_validations = true;
                    container_start_offset = event.raw_xml_offset;
                    if (const std::optional<std::string_view> count =
                            attribute_value(event.raw_xml, "count")) {
                        declared_count = parse_unsigned_decimal(*count);
                        if (!declared_count.has_value()) {
                            throw FastXlsxError(
                                "worksheet data validation count is not an unsigned integer");
                        }
                    }
                    if (event.self_closing) {
                        if (declared_count.has_value() && *declared_count != 0) {
                            throw FastXlsxError(
                                "self-closing data validation container must have count zero");
                        }
                        plan = WorksheetDataValidationRewritePlan {
                            WorksheetDataValidationRewritePlan::Action::ExpandSelfClosingContainer,
                            event.raw_xml_offset,
                            event.raw_xml_offset,
                            1};
                    }
                }
            } else if (top_level && event.element_name == "dataValidations") {
                throw FastXlsxError(
                    "worksheet data validation metadata appears before sheetData");
            }

            if (direct_data_validation_child) {
                if (event.element_name != "dataValidation") {
                    throw FastXlsxError(
                        "data validation container has an unsupported child element");
                }
                ++child_count;
            }

            if (!event.self_closing) {
                metadata_stack.emplace_back(event.element_name);
            }
        });

    if (!metadata_stack.empty()) {
        throw FastXlsxError("worksheet data validation metadata ended inside an open element");
    }
    if (!saw_sheet_data_end || !saw_worksheet_end) {
        throw FastXlsxError(
            "data validation edit requires sheetData and a closing worksheet root");
    }
    if (saw_data_validations) {
        if (!plan.has_value()) {
            throw FastXlsxError(
                "worksheet data validation container has no closing boundary");
        }
        return *plan;
    }
    return WorksheetDataValidationRewritePlan {
        WorksheetDataValidationRewritePlan::Action::InsertContainerBefore,
        first_after_data_validations_offset.value_or(worksheet_end_offset),
        0,
        1};
}

WorksheetAutoFilterRewritePlan plan_worksheet_auto_filter_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk)
{
    bool saw_sheet_data_end = false;
    bool saw_worksheet_end = false;
    bool saw_auto_filter = false;
    int last_suffix_rank = 0;
    std::vector<std::string> metadata_stack;
    std::optional<std::uint64_t> first_after_auto_filter_offset;
    std::optional<WorksheetAutoFilterRewritePlan> existing_plan;
    std::uint64_t auto_filter_start_offset = 0;
    std::uint64_t worksheet_end_offset = 0;

    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::SheetDataEnd) {
                saw_sheet_data_end = true;
                return;
            }
            if (event.kind == WorksheetEventKind::WorksheetEnd) {
                if (!event.self_closing) {
                    saw_worksheet_end = true;
                    worksheet_end_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind != WorksheetEventKind::Metadata) {
                return;
            }

            const bool closing = is_closing_tag(event.raw_xml);
            if (closing) {
                if (metadata_stack.empty() || metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "worksheet auto-filter metadata contains mismatched element nesting");
                }
                if (metadata_stack.size() == 1 && event.element_name == "autoFilter") {
                    if (!saw_auto_filter || existing_plan.has_value()) {
                        throw FastXlsxError(
                            "worksheet contains duplicate or ambiguous auto-filter metadata");
                    }
                    existing_plan = WorksheetAutoFilterRewritePlan {
                        true, auto_filter_start_offset, event_end_offset(event)};
                }
                metadata_stack.pop_back();
                return;
            }

            const bool top_level = metadata_stack.empty();
            if (top_level && saw_sheet_data_end) {
                const std::optional<int> rank =
                    worksheet_suffix_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet contains top-level suffix metadata whose position relative "
                        "to autoFilter is unsupported");
                }
                if (*rank < last_suffix_rank) {
                    throw FastXlsxError(
                        "worksheet top-level suffix metadata is not in schema order");
                }
                last_suffix_rank = *rank;
                if (*rank > auto_filter_schema_rank
                    && !first_after_auto_filter_offset.has_value()) {
                    first_after_auto_filter_offset = event.raw_xml_offset;
                }
                if (event.element_name == "autoFilter") {
                    if (saw_auto_filter) {
                        throw FastXlsxError(
                            "worksheet contains duplicate autoFilter elements");
                    }
                    saw_auto_filter = true;
                    const std::optional<std::string_view> reference =
                        attribute_value(event.raw_xml, "ref");
                    if (!reference.has_value() || reference->empty()) {
                        throw FastXlsxError(
                            "existing worksheet autoFilter is missing its ref");
                    }
                    if (!parse_a1_range(*reference).has_value()) {
                        throw FastXlsxError(
                            "existing worksheet autoFilter ref is not a valid A1 range");
                    }
                    auto_filter_start_offset = event.raw_xml_offset;
                    if (event.self_closing) {
                        existing_plan = WorksheetAutoFilterRewritePlan {
                            true, event.raw_xml_offset, event_end_offset(event)};
                    }
                }
            } else if (top_level && event.element_name == "autoFilter") {
                throw FastXlsxError(
                    "worksheet autoFilter metadata appears before sheetData");
            } else if (!top_level && event.element_name == "autoFilter") {
                throw FastXlsxError(
                    "worksheet autoFilter metadata is nested below another element");
            }

            if (!event.self_closing) {
                metadata_stack.emplace_back(event.element_name);
            }
        });

    if (!metadata_stack.empty()) {
        throw FastXlsxError("worksheet auto-filter metadata ended inside an open element");
    }
    if (!saw_sheet_data_end || !saw_worksheet_end) {
        throw FastXlsxError(
            "auto-filter edit requires sheetData and a closing worksheet root");
    }
    if (saw_auto_filter) {
        if (!existing_plan.has_value()) {
            throw FastXlsxError(
                "worksheet autoFilter element has no closing boundary");
        }
        return *existing_plan;
    }
    return WorksheetAutoFilterRewritePlan {
        false,
        first_after_auto_filter_offset.value_or(worksheet_end_offset),
        first_after_auto_filter_offset.value_or(worksheet_end_offset)};
}

std::optional<WorksheetFreezePaneRewritePlan>
plan_worksheet_freeze_pane_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::uint32_t row_split,
    std::uint32_t column_split,
    WorksheetFreezePaneRewriteOperation operation)
{
    validate_freeze_pane_split(row_split, column_split);
    if (operation == WorksheetFreezePaneRewriteOperation::Set
        && row_split == 0 && column_split == 0) {
        throw FastXlsxError("freeze-pane set requires a non-zero split");
    }

    bool saw_worksheet_start = false;
    bool saw_sheet_data_start = false;
    bool saw_worksheet_end = false;
    bool saw_sheet_views = false;
    bool sheet_views_self_closing = false;
    bool saw_primary_sheet_view = false;
    bool primary_sheet_view_self_closing = false;
    bool inside_primary_sheet_view = false;
    bool open_primary_pane = false;
    int last_prefix_rank = 0;
    int last_primary_child_rank = 0;
    std::size_t primary_sheet_view_depth = 0;
    std::vector<std::string> metadata_stack;
    std::unordered_set<std::uint64_t> workbook_view_ids;
    std::string worksheet_element_prefix;
    std::string sheet_views_element_prefix;
    std::string primary_sheet_view_element_prefix;
    std::string primary_pane_element_prefix;
    std::optional<std::uint64_t> first_after_sheet_views_offset;
    std::optional<std::uint64_t> sheet_views_start_offset;
    std::optional<std::uint64_t> sheet_views_end_offset;
    std::optional<std::uint64_t> sheet_views_close_offset;
    std::optional<std::uint64_t> primary_sheet_view_start_offset;
    std::optional<std::uint64_t> primary_sheet_view_end_offset;
    std::optional<std::uint64_t> primary_sheet_view_close_offset;
    std::optional<std::uint64_t> first_primary_child_offset;
    std::optional<std::uint64_t> primary_pane_start_offset;
    std::optional<std::uint64_t> primary_pane_end_offset;
    std::uint64_t worksheet_end_offset = 0;

    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::WorksheetStart) {
                if (saw_worksheet_start) {
                    throw FastXlsxError(
                        "freeze-pane edit encountered duplicate worksheet roots");
                }
                saw_worksheet_start = true;
                worksheet_element_prefix =
                    xml_element_prefix(event.raw_xml, "worksheet");
                return;
            }
            if (event.kind == WorksheetEventKind::SheetDataStart) {
                if (saw_sheet_data_start || !metadata_stack.empty()) {
                    throw FastXlsxError(
                        "freeze-pane edit encountered ambiguous sheetData metadata");
                }
                saw_sheet_data_start = true;
                if (!first_after_sheet_views_offset.has_value()) {
                    first_after_sheet_views_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind == WorksheetEventKind::WorksheetEnd) {
                if (!event.self_closing) {
                    saw_worksheet_end = true;
                    worksheet_end_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind != WorksheetEventKind::Metadata) {
                const bool inside_sheet_views = !metadata_stack.empty()
                    && metadata_stack.front() == "sheetViews";
                if (!inside_sheet_views) {
                    return;
                }
                if (event.kind == WorksheetEventKind::RawText) {
                    if (!std::all_of(event.raw_xml.begin(), event.raw_xml.end(),
                            [](char character) { return is_xml_space(character); })) {
                        throw FastXlsxError(
                            "worksheet sheetViews metadata contains non-whitespace text");
                    }
                    return;
                }
                if (event.kind == WorksheetEventKind::Comment
                    || event.kind == WorksheetEventKind::ProcessingInstruction) {
                    return;
                }
                throw FastXlsxError(
                    "worksheet sheetViews metadata contains unsupported non-element content");
            }

            const bool closing = is_closing_tag(event.raw_xml);
            if (closing) {
                if (metadata_stack.empty() || metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "worksheet sheetViews metadata contains mismatched element nesting");
                }
                if (event.element_name == "pane" && open_primary_pane) {
                    if (!inside_primary_sheet_view
                        || metadata_stack.size() != primary_sheet_view_depth + 1U
                        || xml_element_prefix(event.raw_xml, "pane")
                            != primary_pane_element_prefix) {
                        throw FastXlsxError(
                            "worksheet primary frozen pane has mismatched QName nesting");
                    }
                    primary_pane_end_offset = event_end_offset(event);
                    open_primary_pane = false;
                } else if (event.element_name == "sheetView"
                    && inside_primary_sheet_view
                    && metadata_stack.size() == primary_sheet_view_depth) {
                    if (open_primary_pane
                        || xml_element_prefix(event.raw_xml, "sheetView")
                            != primary_sheet_view_element_prefix) {
                        throw FastXlsxError(
                            "worksheet primary sheetView has mismatched QName nesting");
                    }
                    primary_sheet_view_close_offset = event.raw_xml_offset;
                    primary_sheet_view_end_offset = event_end_offset(event);
                    inside_primary_sheet_view = false;
                    primary_sheet_view_depth = 0;
                } else if (event.element_name == "sheetViews") {
                    if (metadata_stack.size() != 1U
                        || xml_element_prefix(event.raw_xml, "sheetViews")
                            != sheet_views_element_prefix) {
                        throw FastXlsxError(
                            "worksheet sheetViews container has mismatched QName nesting");
                    }
                    sheet_views_close_offset = event.raw_xml_offset;
                    sheet_views_end_offset = event_end_offset(event);
                }
                metadata_stack.pop_back();
                return;
            }

            const bool top_level = metadata_stack.empty();
            const bool direct_sheet_view = metadata_stack.size() == 1U
                && metadata_stack.front() == "sheetViews";
            const bool direct_primary_child = inside_primary_sheet_view
                && metadata_stack.size() == primary_sheet_view_depth;

            if (top_level && !saw_sheet_data_start) {
                const std::optional<int> rank =
                    worksheet_prefix_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet contains unsupported top-level metadata before sheetData");
                }
                if (*rank < last_prefix_rank) {
                    throw FastXlsxError(
                        "worksheet top-level prefix metadata is not in schema order");
                }
                last_prefix_rank = *rank;
                if (*rank > 3 && !first_after_sheet_views_offset.has_value()) {
                    first_after_sheet_views_offset = event.raw_xml_offset;
                }
                if (event.element_name == "sheetViews") {
                    if (saw_sheet_views) {
                        throw FastXlsxError(
                            "worksheet contains duplicate sheetViews containers");
                    }
                    saw_sheet_views = true;
                    sheet_views_start_offset = event.raw_xml_offset;
                    sheet_views_element_prefix =
                        xml_element_prefix(event.raw_xml, "sheetViews");
                    if (sheet_views_element_prefix != worksheet_element_prefix) {
                        throw FastXlsxError(
                            "worksheet sheetViews QName prefix differs from its root");
                    }
                    if (event.self_closing) {
                        sheet_views_self_closing = true;
                        sheet_views_end_offset = event_end_offset(event);
                    }
                }
            } else if (top_level && event.element_name == "sheetViews") {
                throw FastXlsxError(
                    "worksheet sheetViews metadata appears after sheetData");
            } else if (!top_level && event.element_name == "sheetViews") {
                throw FastXlsxError(
                    "worksheet sheetViews metadata is nested below another element");
            }

            if (direct_sheet_view) {
                if (event.element_name != "sheetView") {
                    throw FastXlsxError(
                        "worksheet sheetViews container has an unsupported child element");
                }
                const std::string sheet_view_prefix =
                    xml_element_prefix(event.raw_xml, "sheetView");
                if (sheet_view_prefix != sheet_views_element_prefix) {
                    throw FastXlsxError(
                        "worksheet sheetView QName prefix differs from its container");
                }
                const std::optional<std::string_view> workbook_view_id_text =
                    attribute_value(event.raw_xml, "workbookViewId");
                const std::optional<std::uint64_t> workbook_view_id =
                    workbook_view_id_text.has_value()
                    ? parse_unsigned_decimal(*workbook_view_id_text)
                    : std::nullopt;
                if (!workbook_view_id.has_value()) {
                    throw FastXlsxError(
                        "worksheet sheetView has no valid workbookViewId");
                }
                if (!workbook_view_ids.emplace(*workbook_view_id).second) {
                    throw FastXlsxError(
                        "worksheet contains duplicate sheetView workbookViewId values");
                }
                if (*workbook_view_id == 0) {
                    saw_primary_sheet_view = true;
                    primary_sheet_view_start_offset = event.raw_xml_offset;
                    primary_sheet_view_element_prefix = sheet_view_prefix;
                    if (event.self_closing) {
                        primary_sheet_view_self_closing = true;
                        primary_sheet_view_end_offset = event_end_offset(event);
                    } else {
                        inside_primary_sheet_view = true;
                        primary_sheet_view_depth = metadata_stack.size() + 1U;
                    }
                }
            } else if (direct_primary_child) {
                const std::optional<int> rank =
                    sheet_view_child_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet primary sheetView has an unsupported child element");
                }
                if (*rank < last_primary_child_rank) {
                    throw FastXlsxError(
                        "worksheet primary sheetView children are not in schema order");
                }
                last_primary_child_rank = *rank;
                if (!first_primary_child_offset.has_value()) {
                    first_primary_child_offset = event.raw_xml_offset;
                }

                if (event.element_name == "pane") {
                    if (primary_pane_start_offset.has_value()) {
                        throw FastXlsxError(
                            "worksheet primary sheetView contains duplicate panes");
                    }
                    primary_pane_element_prefix =
                        xml_element_prefix(event.raw_xml, "pane");
                    if (primary_pane_element_prefix
                        != primary_sheet_view_element_prefix) {
                        throw FastXlsxError(
                            "worksheet pane QName prefix differs from its sheetView");
                    }
                    audit_existing_frozen_pane(event.raw_xml);
                    primary_pane_start_offset = event.raw_xml_offset;
                    if (event.self_closing) {
                        primary_pane_end_offset = event_end_offset(event);
                    } else {
                        open_primary_pane = true;
                    }
                } else if (event.element_name == "selection") {
                    if (xml_element_prefix(event.raw_xml, "selection")
                        != primary_sheet_view_element_prefix) {
                        throw FastXlsxError(
                            "worksheet selection QName prefix differs from its sheetView");
                    }
                    if (const std::optional<std::string_view> pane =
                            attribute_value(event.raw_xml, "pane")) {
                        if (!selection_pane_is_preservable(
                                *pane, row_split, column_split, operation)) {
                            throw FastXlsxError(
                                "worksheet selection references a pane that the requested "
                                "freeze-pane edit cannot preserve");
                        }
                    }
                } else if (event.element_name == "pivotSelection") {
                    throw FastXlsxError(
                        "worksheet primary sheetView contains unsupported pivotSelection metadata");
                } else if (xml_element_prefix(event.raw_xml, "extLst")
                    != primary_sheet_view_element_prefix) {
                    throw FastXlsxError(
                        "worksheet sheetView extLst QName prefix differs from its sheetView");
                }
            } else if (inside_primary_sheet_view && !metadata_stack.empty()
                && (metadata_stack.back() == "pane"
                    || metadata_stack.back() == "selection")) {
                throw FastXlsxError(
                    "worksheet primary sheetView contains a non-empty pane or selection");
            }

            if (!event.self_closing) {
                metadata_stack.emplace_back(event.element_name);
            }
        });

    if (!metadata_stack.empty() || inside_primary_sheet_view || open_primary_pane) {
        throw FastXlsxError(
            "worksheet sheetViews metadata ended inside an open element");
    }
    if (!saw_worksheet_start || !saw_sheet_data_start || !saw_worksheet_end) {
        throw FastXlsxError(
            "freeze-pane edit requires a worksheet root and sheetData");
    }

    if (!saw_sheet_views) {
        if (operation == WorksheetFreezePaneRewriteOperation::Clear) {
            return std::nullopt;
        }
        const std::uint64_t insertion_offset =
            first_after_sheet_views_offset.value_or(worksheet_end_offset);
        return WorksheetFreezePaneRewritePlan {
            WorksheetFreezePaneRewritePlan::Action::InsertSheetViewsBefore,
            insertion_offset,
            insertion_offset,
            worksheet_element_prefix};
    }
    if (sheet_views_self_closing) {
        if (operation == WorksheetFreezePaneRewriteOperation::Clear) {
            return std::nullopt;
        }
        return WorksheetFreezePaneRewritePlan {
            WorksheetFreezePaneRewritePlan::Action::ExpandSheetViewsContainer,
            *sheet_views_start_offset,
            *sheet_views_end_offset,
            sheet_views_element_prefix};
    }
    if (!sheet_views_close_offset.has_value() || !sheet_views_end_offset.has_value()) {
        throw FastXlsxError("worksheet sheetViews container has no closing boundary");
    }
    if (!saw_primary_sheet_view) {
        if (operation == WorksheetFreezePaneRewriteOperation::Clear) {
            return std::nullopt;
        }
        return WorksheetFreezePaneRewritePlan {
            WorksheetFreezePaneRewritePlan::Action::AppendPrimarySheetView,
            *sheet_views_close_offset,
            *sheet_views_close_offset,
            sheet_views_element_prefix};
    }
    if (primary_sheet_view_self_closing) {
        if (operation == WorksheetFreezePaneRewriteOperation::Clear) {
            return std::nullopt;
        }
        return WorksheetFreezePaneRewritePlan {
            WorksheetFreezePaneRewritePlan::Action::ExpandPrimarySheetView,
            *primary_sheet_view_start_offset,
            *primary_sheet_view_end_offset,
            primary_sheet_view_element_prefix};
    }
    if (!primary_sheet_view_close_offset.has_value()
        || !primary_sheet_view_end_offset.has_value()) {
        throw FastXlsxError("worksheet primary sheetView has no closing boundary");
    }
    if (!primary_pane_start_offset.has_value()) {
        if (operation == WorksheetFreezePaneRewriteOperation::Clear) {
            return std::nullopt;
        }
        const std::uint64_t insertion_offset = first_primary_child_offset.value_or(
            *primary_sheet_view_close_offset);
        return WorksheetFreezePaneRewritePlan {
            WorksheetFreezePaneRewritePlan::Action::InsertPaneBefore,
            insertion_offset,
            insertion_offset,
            primary_sheet_view_element_prefix};
    }
    if (!primary_pane_end_offset.has_value()) {
        throw FastXlsxError("worksheet primary frozen pane has no closing boundary");
    }
    return WorksheetFreezePaneRewritePlan {
        operation == WorksheetFreezePaneRewriteOperation::Set
            ? WorksheetFreezePaneRewritePlan::Action::ReplacePane
            : WorksheetFreezePaneRewritePlan::Action::RemovePane,
        *primary_pane_start_offset,
        *primary_pane_end_offset,
        primary_sheet_view_element_prefix};
}

std::optional<WorksheetMergedCellRewritePlan>
plan_worksheet_merged_cell_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    CellRange range,
    WorksheetMergedCellRewriteOperation operation)
{
    validate_merged_cell_range(range);
    const A1Range target = a1_range_from_cell_range(range);

    bool saw_sheet_data_end = false;
    bool saw_worksheet_start = false;
    bool saw_worksheet_end = false;
    bool saw_merge_cells = false;
    bool merge_cells_self_closing = false;
    int last_suffix_rank = 0;
    std::vector<std::string> metadata_stack;
    std::vector<ExistingMergedCell> existing_cells;
    std::optional<std::uint64_t> declared_count;
    std::optional<std::uint64_t> first_after_merged_cells_offset;
    std::optional<std::size_t> target_index;
    std::optional<A1Range> open_merge_cell_range;
    std::string worksheet_element_prefix;
    std::string merge_cells_element_prefix;
    std::string open_merge_cell_element_prefix;
    std::uint64_t open_merge_cell_offset = 0;
    std::uint64_t merge_cells_start_offset = 0;
    std::uint64_t merge_cells_end_offset = 0;
    std::uint64_t merge_cells_close_offset = 0;
    std::uint64_t worksheet_end_offset = 0;
    std::uint64_t child_count = 0;

    const auto record_merged_cell = [&](const A1Range& existing,
                                        std::uint64_t source_offset,
                                        std::uint64_t source_end_offset) {
        existing_cells.push_back(
            ExistingMergedCell {existing, source_offset, source_end_offset});
    };

    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::WorksheetStart) {
                if (saw_worksheet_start) {
                    throw FastXlsxError(
                        "worksheet merged-cell edit encountered duplicate worksheet roots");
                }
                saw_worksheet_start = true;
                worksheet_element_prefix =
                    xml_element_prefix(event.raw_xml, "worksheet");
                return;
            }
            if (event.kind == WorksheetEventKind::SheetDataEnd) {
                saw_sheet_data_end = true;
                return;
            }
            if (event.kind == WorksheetEventKind::WorksheetEnd) {
                if (!event.self_closing) {
                    saw_worksheet_end = true;
                    worksheet_end_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind != WorksheetEventKind::Metadata) {
                const bool inside_merge_cells = !metadata_stack.empty()
                    && metadata_stack.front() == "mergeCells";
                if (!inside_merge_cells) {
                    return;
                }
                if (event.kind == WorksheetEventKind::RawText) {
                    if (!std::all_of(event.raw_xml.begin(), event.raw_xml.end(),
                            [](char character) { return is_xml_space(character); })) {
                        throw FastXlsxError(
                            "worksheet merged-cell metadata contains non-whitespace text");
                    }
                    return;
                }
                if (event.kind == WorksheetEventKind::Comment
                    || event.kind == WorksheetEventKind::ProcessingInstruction) {
                    return;
                }
                throw FastXlsxError(
                    "worksheet merged-cell metadata contains unsupported non-element content");
            }

            const bool closing = is_closing_tag(event.raw_xml);
            if (closing) {
                if (metadata_stack.empty() || metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "worksheet merged-cell metadata contains mismatched element nesting");
                }
                if (event.element_name == "mergeCell") {
                    if (metadata_stack.size() != 2
                        || metadata_stack.front() != "mergeCells"
                        || !open_merge_cell_range.has_value()) {
                        throw FastXlsxError(
                            "worksheet mergeCell appears outside the mergeCells container");
                    }
                    if (xml_element_prefix(event.raw_xml, "mergeCell")
                        != open_merge_cell_element_prefix) {
                        throw FastXlsxError(
                            "worksheet mergeCell opening and closing QName prefixes differ");
                    }
                    record_merged_cell(*open_merge_cell_range,
                        open_merge_cell_offset, event_end_offset(event));
                    open_merge_cell_range.reset();
                    open_merge_cell_element_prefix.clear();
                } else if (event.element_name == "mergeCells") {
                    if (metadata_stack.size() != 1 || !saw_merge_cells) {
                        throw FastXlsxError(
                            "worksheet contains ambiguous mergeCells metadata");
                    }
                    if (xml_element_prefix(event.raw_xml, "mergeCells")
                        != merge_cells_element_prefix) {
                        throw FastXlsxError(
                            "worksheet mergeCells opening and closing QName prefixes differ");
                    }
                    if (declared_count.has_value() && *declared_count != child_count) {
                        throw FastXlsxError(
                            "worksheet merged-cell count does not match its direct children");
                    }
                    merge_cells_close_offset = event.raw_xml_offset;
                    merge_cells_end_offset = event_end_offset(event);
                }
                metadata_stack.pop_back();
                return;
            }

            const bool top_level = metadata_stack.empty();
            const bool direct_merge_cell_child = metadata_stack.size() == 1
                && metadata_stack.front() == "mergeCells";
            if (top_level && saw_sheet_data_end) {
                const std::optional<int> rank =
                    worksheet_suffix_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet contains top-level suffix metadata whose position relative "
                        "to merged cells is unsupported");
                }
                if (*rank < last_suffix_rank) {
                    throw FastXlsxError(
                        "worksheet top-level suffix metadata is not in schema order");
                }
                last_suffix_rank = *rank;
                if (*rank > merged_cells_schema_rank
                    && !first_after_merged_cells_offset.has_value()) {
                    first_after_merged_cells_offset = event.raw_xml_offset;
                }
                if (event.element_name == "mergeCells") {
                    if (saw_merge_cells) {
                        throw FastXlsxError(
                            "worksheet contains duplicate mergeCells containers");
                    }
                    saw_merge_cells = true;
                    merge_cells_start_offset = event.raw_xml_offset;
                    merge_cells_element_prefix =
                        xml_element_prefix(event.raw_xml, "mergeCells");
                    if (const std::optional<std::string_view> count =
                            attribute_value(event.raw_xml, "count")) {
                        declared_count = parse_unsigned_decimal(*count);
                        if (!declared_count.has_value()) {
                            throw FastXlsxError(
                                "worksheet merged-cell count is not an unsigned integer");
                        }
                    }
                    if (event.self_closing) {
                        merge_cells_self_closing = true;
                        merge_cells_end_offset = event_end_offset(event);
                        if (declared_count.has_value() && *declared_count != 0) {
                            throw FastXlsxError(
                                "self-closing mergeCells container must have count zero");
                        }
                    }
                }
            } else if (top_level && event.element_name == "mergeCells") {
                throw FastXlsxError(
                    "worksheet merged-cell metadata appears before sheetData");
            } else if (!top_level && event.element_name == "mergeCells") {
                throw FastXlsxError(
                    "worksheet mergeCells metadata is nested below another element");
            }

            if (direct_merge_cell_child) {
                if (event.element_name != "mergeCell") {
                    throw FastXlsxError(
                        "mergeCells container has an unsupported child element");
                }
                const std::string child_element_prefix =
                    xml_element_prefix(event.raw_xml, "mergeCell");
                if (child_element_prefix != merge_cells_element_prefix) {
                    throw FastXlsxError(
                        "worksheet mergeCell QName prefix differs from its container");
                }
                const std::optional<std::string_view> reference =
                    attribute_value(event.raw_xml, "ref");
                if (!reference.has_value() || reference->empty()) {
                    throw FastXlsxError("existing worksheet mergeCell is missing its ref");
                }
                const std::optional<A1Range> existing = parse_a1_range(*reference);
                if (!existing.has_value() || a1_range_is_single_cell(*existing)) {
                    throw FastXlsxError(
                        "existing worksheet mergeCell ref is not a valid multi-cell A1 range");
                }
                if (child_count == std::numeric_limits<std::uint64_t>::max()) {
                    throw FastXlsxError("worksheet merged-cell count exceeds supported range");
                }
                ++child_count;
                if (event.self_closing) {
                    record_merged_cell(
                        *existing, event.raw_xml_offset, event_end_offset(event));
                } else {
                    open_merge_cell_range = existing;
                    open_merge_cell_element_prefix = child_element_prefix;
                    open_merge_cell_offset = event.raw_xml_offset;
                }
            } else if (!top_level && !metadata_stack.empty()
                && metadata_stack.back() == "mergeCell") {
                throw FastXlsxError(
                    "worksheet mergeCell must not contain child elements");
            }

            if (!event.self_closing) {
                metadata_stack.emplace_back(event.element_name);
            }
        });

    if (!metadata_stack.empty() || open_merge_cell_range.has_value()) {
        throw FastXlsxError("worksheet merged-cell metadata ended inside an open element");
    }
    if (!saw_worksheet_start || !saw_sheet_data_end || !saw_worksheet_end) {
        throw FastXlsxError(
            "merged-cell edit requires a worksheet root and closed sheetData");
    }

    audit_existing_merged_cell_overlaps(existing_cells);
    for (std::size_t index = 0; index < existing_cells.size(); ++index) {
        const A1Range& existing = existing_cells[index].range;
        if (!a1_ranges_overlap(existing, target)) {
            continue;
        }
        if (operation == WorksheetMergedCellRewriteOperation::Merge) {
            throw FastXlsxError(
                "requested merged-cell range overlaps an existing merged range");
        }
        if (!a1_ranges_equal(existing, target)) {
            throw FastXlsxError(
                "requested unmerge range partially overlaps an existing merged range");
        }
        target_index = index;
    }
    if (!saw_merge_cells) {
        if (operation == WorksheetMergedCellRewriteOperation::Unmerge) {
            return std::nullopt;
        }
        const std::uint64_t insertion_offset =
            first_after_merged_cells_offset.value_or(worksheet_end_offset);
        return WorksheetMergedCellRewritePlan {
            WorksheetMergedCellRewritePlan::Action::InsertContainerBefore,
            insertion_offset,
            insertion_offset,
            0,
            1,
            worksheet_element_prefix};
    }
    if (merge_cells_self_closing) {
        if (operation == WorksheetMergedCellRewriteOperation::Unmerge) {
            return std::nullopt;
        }
        return WorksheetMergedCellRewritePlan {
            WorksheetMergedCellRewritePlan::Action::ExpandSelfClosingContainer,
            merge_cells_start_offset,
            merge_cells_end_offset,
            merge_cells_start_offset,
            1,
            merge_cells_element_prefix};
    }
    if (merge_cells_close_offset == 0 || merge_cells_end_offset == 0) {
        throw FastXlsxError("worksheet mergeCells container has no closing boundary");
    }
    if (operation == WorksheetMergedCellRewriteOperation::Merge) {
        if (child_count == std::numeric_limits<std::uint64_t>::max()) {
            throw FastXlsxError("worksheet merged-cell count exceeds supported range");
        }
        return WorksheetMergedCellRewritePlan {
            WorksheetMergedCellRewritePlan::Action::AppendBeforeContainerClose,
            merge_cells_close_offset,
            merge_cells_close_offset,
            merge_cells_start_offset,
            child_count + 1U,
            merge_cells_element_prefix};
    }
    if (!target_index.has_value()) {
        return std::nullopt;
    }
    if (child_count == 1) {
        return WorksheetMergedCellRewritePlan {
            WorksheetMergedCellRewritePlan::Action::RemoveContainer,
            merge_cells_start_offset,
            merge_cells_end_offset,
            merge_cells_start_offset,
            0,
            merge_cells_element_prefix};
    }
    const ExistingMergedCell& target_cell = existing_cells[*target_index];
    return WorksheetMergedCellRewritePlan {
        WorksheetMergedCellRewritePlan::Action::RemoveChild,
        target_cell.source_offset,
        target_cell.source_end_offset,
        merge_cells_start_offset,
        child_count - 1U,
        merge_cells_element_prefix};
}

void write_worksheet_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view hyperlink_xml,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path,
    bool ensure_relationship_namespace)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create staged worksheet hyperlink metadata file");
    }

    bool applied = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (is_synthetic_self_closing_end(event)) {
                return;
            }
            if (!applied && event.raw_xml_offset == plan.source_offset) {
                switch (plan.action) {
                case WorksheetInternalHyperlinkRewriteAction::InsertContainerBefore:
                    write_bytes(output, "<hyperlinks>");
                    write_bytes(output, hyperlink_xml);
                    write_bytes(output, "</hyperlinks>");
                    break;
                case WorksheetInternalHyperlinkRewriteAction::AppendBeforeContainerClose:
                    write_bytes(output, hyperlink_xml);
                    break;
                case WorksheetInternalHyperlinkRewriteAction::ExpandSelfClosingContainer:
                    write_bytes(output, expand_self_closing_tag(event.raw_xml));
                    write_bytes(output, hyperlink_xml);
                    write_bytes(output, "</hyperlinks>");
                    applied = true;
                    return;
                }
                applied = true;
            }
            if (ensure_relationship_namespace
                && event.kind == WorksheetEventKind::WorksheetStart) {
                write_bytes(output,
                    worksheet_root_with_relationship_namespace(event.raw_xml));
            } else {
                write_bytes(output, event.raw_xml);
            }
        });

    if (!applied) {
        throw FastXlsxError(
            "worksheet hyperlink rewrite did not reach its planned insertion boundary");
    }
    output.flush();
    if (!output) {
        throw FastXlsxError("failed to finalize staged worksheet hyperlink metadata file");
    }
}

void write_worksheet_data_validation_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view data_validation_xml,
    const WorksheetDataValidationRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create staged worksheet data validation metadata file");
    }

    bool applied = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (is_synthetic_self_closing_end(event)) {
                return;
            }

            if (plan.action == WorksheetDataValidationRewritePlan::Action::AppendBeforeContainerClose
                && event.raw_xml_offset == plan.container_start_offset
                && event.kind == WorksheetEventKind::Metadata
                && !is_closing_tag(event.raw_xml)) {
                write_bytes(output, metadata_container_opening_with_count(
                    event.raw_xml, plan.new_count, false, "data validation"));
                return;
            }

            if (applied || event.raw_xml_offset != plan.source_offset) {
                write_bytes(output, event.raw_xml);
                return;
            }

            switch (plan.action) {
            case WorksheetDataValidationRewritePlan::Action::InsertContainerBefore:
                write_bytes(output, "<dataValidations count=\"1\">");
                write_bytes(output, data_validation_xml);
                write_bytes(output, "</dataValidations>");
                write_bytes(output, event.raw_xml);
                break;
            case WorksheetDataValidationRewritePlan::Action::AppendBeforeContainerClose:
                write_bytes(output, data_validation_xml);
                write_bytes(output, event.raw_xml);
                break;
            case WorksheetDataValidationRewritePlan::Action::ExpandSelfClosingContainer:
                write_bytes(output, metadata_container_opening_with_count(
                    event.raw_xml, plan.new_count, true, "data validation"));
                write_bytes(output, data_validation_xml);
                write_bytes(output, "</dataValidations>");
                break;
            }
            applied = true;
        });

    if (!applied) {
        throw FastXlsxError(
            "worksheet data validation rewrite did not reach its planned insertion boundary");
    }
    output.flush();
    if (!output) {
        throw FastXlsxError(
            "failed to finalize staged worksheet data validation metadata file");
    }
}

void write_worksheet_auto_filter_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view auto_filter_xml,
    const WorksheetAutoFilterRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create staged worksheet auto-filter metadata file");
    }

    bool applied = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (is_synthetic_self_closing_end(event)) {
                return;
            }

            if (!applied && event.raw_xml_offset == plan.source_offset) {
                write_bytes(output, auto_filter_xml);
                applied = true;
            }
            if (plan.has_existing_auto_filter
                && event.raw_xml_offset >= plan.source_offset
                && event.raw_xml_offset < plan.source_end_offset) {
                return;
            }
            write_bytes(output, event.raw_xml);
        });

    if (!applied) {
        throw FastXlsxError(
            "worksheet auto-filter rewrite did not reach its planned boundary");
    }
    output.flush();
    if (!output) {
        throw FastXlsxError(
            "failed to finalize staged worksheet auto-filter metadata file");
    }
}

void write_worksheet_freeze_pane_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view pane_xml,
    const WorksheetFreezePaneRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create staged worksheet freeze-pane metadata file");
    }

    const auto write_primary_sheet_view = [&] {
        write_bytes(output, "<");
        write_bytes(output, plan.element_prefix);
        write_bytes(output, "sheetView workbookViewId=\"0\">");
        write_bytes(output, pane_xml);
        write_bytes(output, "</");
        write_bytes(output, plan.element_prefix);
        write_bytes(output, "sheetView>");
    };

    bool applied = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (is_synthetic_self_closing_end(event)) {
                return;
            }

            const bool rewrites_source_range =
                plan.action == WorksheetFreezePaneRewritePlan::Action::ReplacePane
                || plan.action == WorksheetFreezePaneRewritePlan::Action::RemovePane;
            if (rewrites_source_range
                && event.raw_xml_offset >= plan.source_offset
                && event.raw_xml_offset < plan.source_end_offset) {
                if (!applied
                    && plan.action == WorksheetFreezePaneRewritePlan::Action::ReplacePane) {
                    write_bytes(output, pane_xml);
                }
                applied = true;
                return;
            }

            if (applied || event.raw_xml_offset != plan.source_offset) {
                write_bytes(output, event.raw_xml);
                return;
            }

            switch (plan.action) {
            case WorksheetFreezePaneRewritePlan::Action::InsertSheetViewsBefore:
                write_bytes(output, "<");
                write_bytes(output, plan.element_prefix);
                write_bytes(output, "sheetViews>");
                write_primary_sheet_view();
                write_bytes(output, "</");
                write_bytes(output, plan.element_prefix);
                write_bytes(output, "sheetViews>");
                write_bytes(output, event.raw_xml);
                break;
            case WorksheetFreezePaneRewritePlan::Action::ExpandSheetViewsContainer:
                write_bytes(output, expand_self_closing_metadata_tag(
                    event.raw_xml, "sheetViews"));
                write_primary_sheet_view();
                write_bytes(output, "</");
                write_bytes(output, plan.element_prefix);
                write_bytes(output, "sheetViews>");
                break;
            case WorksheetFreezePaneRewritePlan::Action::AppendPrimarySheetView:
                write_primary_sheet_view();
                write_bytes(output, event.raw_xml);
                break;
            case WorksheetFreezePaneRewritePlan::Action::ExpandPrimarySheetView:
                write_bytes(output, expand_self_closing_metadata_tag(
                    event.raw_xml, "sheetView"));
                write_bytes(output, pane_xml);
                write_bytes(output, "</");
                write_bytes(output, plan.element_prefix);
                write_bytes(output, "sheetView>");
                break;
            case WorksheetFreezePaneRewritePlan::Action::InsertPaneBefore:
                write_bytes(output, pane_xml);
                write_bytes(output, event.raw_xml);
                break;
            case WorksheetFreezePaneRewritePlan::Action::ReplacePane:
            case WorksheetFreezePaneRewritePlan::Action::RemovePane:
                throw FastXlsxError(
                    "worksheet freeze-pane rewrite did not reach its planned source range");
            }
            applied = true;
        });

    if (!applied) {
        throw FastXlsxError(
            "worksheet freeze-pane rewrite did not reach its planned boundary");
    }
    output.flush();
    if (!output) {
        throw FastXlsxError(
            "failed to finalize staged worksheet freeze-pane metadata file");
    }
}

void write_worksheet_merged_cell_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view merge_cell_xml,
    const WorksheetMergedCellRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create staged worksheet merged-cell metadata file");
    }

    bool applied = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (is_synthetic_self_closing_end(event)) {
                return;
            }

            const bool rewrites_container_count =
                plan.action
                    == WorksheetMergedCellRewritePlan::Action::AppendBeforeContainerClose
                || plan.action == WorksheetMergedCellRewritePlan::Action::RemoveChild;
            if (rewrites_container_count
                && event.raw_xml_offset == plan.container_start_offset
                && event.kind == WorksheetEventKind::Metadata
                && !is_closing_tag(event.raw_xml)) {
                write_bytes(output, metadata_container_opening_with_count(
                    event.raw_xml, plan.new_count, false, "merged-cell"));
                return;
            }

            const bool removes_source_range =
                plan.action == WorksheetMergedCellRewritePlan::Action::RemoveChild
                || plan.action == WorksheetMergedCellRewritePlan::Action::RemoveContainer;
            if (removes_source_range
                && event.raw_xml_offset >= plan.source_offset
                && event.raw_xml_offset < plan.source_end_offset) {
                applied = true;
                return;
            }

            if (applied || event.raw_xml_offset != plan.source_offset) {
                write_bytes(output, event.raw_xml);
                return;
            }

            switch (plan.action) {
            case WorksheetMergedCellRewritePlan::Action::InsertContainerBefore:
                write_bytes(output, "<");
                write_bytes(output, plan.element_prefix);
                write_bytes(output, "mergeCells count=\"1\">");
                write_bytes(output, merge_cell_xml);
                write_bytes(output, "</");
                write_bytes(output, plan.element_prefix);
                write_bytes(output, "mergeCells>");
                write_bytes(output, event.raw_xml);
                break;
            case WorksheetMergedCellRewritePlan::Action::AppendBeforeContainerClose:
                write_bytes(output, merge_cell_xml);
                write_bytes(output, event.raw_xml);
                break;
            case WorksheetMergedCellRewritePlan::Action::ExpandSelfClosingContainer:
                write_bytes(output, metadata_container_opening_with_count(
                    event.raw_xml, plan.new_count, true, "merged-cell"));
                write_bytes(output, merge_cell_xml);
                write_bytes(output, "</");
                write_bytes(output, plan.element_prefix);
                write_bytes(output, "mergeCells>");
                break;
            case WorksheetMergedCellRewritePlan::Action::RemoveChild:
            case WorksheetMergedCellRewritePlan::Action::RemoveContainer:
                throw FastXlsxError(
                    "worksheet merged-cell removal did not reach its planned source range");
            }
            applied = true;
        });

    if (!applied) {
        throw FastXlsxError(
            "worksheet merged-cell rewrite did not reach its planned boundary");
    }
    output.flush();
    if (!output) {
        throw FastXlsxError(
            "failed to finalize staged worksheet merged-cell metadata file");
    }
}

void write_worksheet_internal_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetInternalHyperlinkRewrite& hyperlink,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    write_worksheet_hyperlink_rewrite(read_next_chunk,
        internal_hyperlink_xml(hyperlink), plan, output_path, false);
}

void write_worksheet_external_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetExternalHyperlinkRewrite& hyperlink,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    write_worksheet_hyperlink_rewrite(read_next_chunk,
        external_hyperlink_xml(hyperlink), plan, output_path, true);
}

} // namespace fastxlsx::detail
