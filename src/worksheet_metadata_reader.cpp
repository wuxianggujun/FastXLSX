#include "worksheet_metadata_reader.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr std::uint32_t max_freeze_pane_row_split = 1048575U;
constexpr std::uint32_t max_freeze_pane_column_split = 16383U;

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
        throw FastXlsxError("worksheet metadata contains an invalid XML tag");
    }
    if (raw_xml[1] == '!' || raw_xml[1] == '?') {
        throw FastXlsxError("worksheet metadata received unsupported declaration markup");
    }

    ParsedTag result;
    result.closing = is_closing_tag(raw_xml);
    std::size_t position = result.closing ? 2U : 1U;
    const std::size_t name_begin = position;
    while (position < raw_xml.size() && !is_ascii_name_boundary(raw_xml[position])) {
        ++position;
    }
    if (position == name_begin) {
        throw FastXlsxError("worksheet metadata contains an empty element name");
    }
    result.qualified_name = raw_xml.substr(name_begin, position - name_begin);
    const std::size_t separator = result.qualified_name.find(':');
    if (separator != std::string_view::npos
        && (separator == 0 || separator + 1U == result.qualified_name.size()
            || result.qualified_name.find(':', separator + 1U) != std::string_view::npos)) {
        throw FastXlsxError("worksheet metadata contains an invalid qualified element name");
    }
    if (separator == std::string_view::npos) {
        result.local_name = result.qualified_name;
        result.prefix = {};
    } else {
        result.local_name = result.qualified_name.substr(separator + 1U);
        result.prefix = result.qualified_name.substr(0, separator + 1U);
    }

    if (result.closing) {
        while (position < raw_xml.size() - 1U && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position != raw_xml.size() - 1U || raw_xml[position] != '>') {
            throw FastXlsxError("worksheet metadata closing tag contains attributes");
        }
        return result;
    }

    while (position < raw_xml.size()) {
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError("worksheet metadata contains an incomplete XML tag");
        }
        if (raw_xml[position] == '>') {
            ++position;
            if (position != raw_xml.size()) {
                throw FastXlsxError("worksheet metadata contains trailing XML tag bytes");
            }
            return result;
        }
        if (raw_xml[position] == '/') {
            ++position;
            while (position < raw_xml.size() - 1U && is_xml_space(raw_xml[position])) {
                ++position;
            }
            if (position != raw_xml.size() - 1U || raw_xml[position] != '>') {
                throw FastXlsxError("worksheet metadata contains an invalid self-closing tag tail");
            }
            result.self_closing = true;
            ++position;
            return result;
        }

        const std::size_t attribute_begin = position;
        while (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '=' && raw_xml[position] != '/'
            && raw_xml[position] != '>') {
            ++position;
        }
        if (position == attribute_begin) {
            throw FastXlsxError("worksheet metadata contains an empty attribute name");
        }
        const std::string_view attribute_name =
            raw_xml.substr(attribute_begin, position - attribute_begin);
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size() || raw_xml[position] != '=') {
            throw FastXlsxError("worksheet metadata contains an attribute without a value");
        }
        ++position;
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size() || (raw_xml[position] != '"' && raw_xml[position] != '\'')) {
            throw FastXlsxError("worksheet metadata contains an unquoted attribute value");
        }
        const char quote = raw_xml[position++];
        const std::size_t value_begin = position;
        while (position < raw_xml.size() && raw_xml[position] != quote) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError("worksheet metadata contains an unterminated attribute value");
        }
        const std::string_view attribute_value = raw_xml.substr(value_begin, position - value_begin);
        ++position;
        if (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '/' && raw_xml[position] != '>') {
            throw FastXlsxError("worksheet metadata attributes are not separated by whitespace");
        }
        for (const auto& [existing_name, existing_value] : result.attributes) {
            (void)existing_value;
            if (existing_name == attribute_name) {
                throw FastXlsxError("worksheet metadata contains a duplicate attribute");
            }
        }
        result.attributes.emplace_back(attribute_name, attribute_value);
    }

    throw FastXlsxError("worksheet metadata contains an incomplete XML tag");
}

std::optional<std::string_view> attribute_value(
    const ParsedTag& tag, std::string_view requested_name)
{
    for (const auto& [name, value] : tag.attributes) {
        if (name == requested_name) {
            return value;
        }
    }
    return std::nullopt;
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
    while (position < text.size()
        && std::isalpha(static_cast<unsigned char>(text[position])) != 0) {
        const char upper = static_cast<char>(
            std::toupper(static_cast<unsigned char>(text[position])));
        if (upper < 'A' || upper > 'Z') {
            return std::nullopt;
        }
        column = column * 26U + static_cast<std::uint32_t>(upper - 'A' + 1);
        if (column > 16384U) {
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
    while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
        row = row * 10U + static_cast<std::uint32_t>(text[position] - '0');
        if (row > 1048576U) {
            return std::nullopt;
        }
        ++position;
    }
    if (position == row_begin || position != text.size() || row == 0) {
        return std::nullopt;
    }
    return A1Coordinate {static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)};
}

struct A1Range {
    A1Coordinate first;
    A1Coordinate last;
};

std::optional<A1Range> parse_a1_range(std::string_view reference)
{
    const std::size_t separator = reference.find(':');
    if (separator != std::string_view::npos
        && reference.find(':', separator + 1U) != std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<A1Coordinate> first = parse_a1_coordinate(
        separator == std::string_view::npos ? reference : reference.substr(0, separator));
    const std::optional<A1Coordinate> last = separator == std::string_view::npos
        ? first
        : parse_a1_coordinate(reference.substr(separator + 1U));
    if (!first.has_value() || !last.has_value() || first->row > last->row
        || first->column > last->column) {
        return std::nullopt;
    }
    return A1Range {*first, *last};
}

CellRange to_cell_range(const A1Range& range) noexcept
{
    return CellRange {
        range.first.row,
        range.first.column,
        range.last.row,
        range.last.column,
    };
}

bool is_single_cell(const A1Range& range) noexcept
{
    return range.first.row == range.last.row && range.first.column == range.last.column;
}

void audit_merged_cell_overlaps(const std::vector<A1Range>& ranges)
{
    std::vector<std::size_t> ordered_indices(ranges.size());
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        ordered_indices[index] = index;
    }
    std::sort(ordered_indices.begin(), ordered_indices.end(),
        [&ranges](std::size_t left, std::size_t right) {
            const A1Range& lhs = ranges[left];
            const A1Range& rhs = ranges[right];
            if (lhs.first.row != rhs.first.row) {
                return lhs.first.row < rhs.first.row;
            }
            if (lhs.first.column != rhs.first.column) {
                return lhs.first.column < rhs.first.column;
            }
            if (lhs.last.row != rhs.last.row) {
                return lhs.last.row < rhs.last.row;
            }
            return lhs.last.column < rhs.last.column;
        });

    std::multimap<std::uint32_t, std::size_t> active_by_last_row;
    std::map<std::uint32_t, std::size_t> active_by_first_column;
    for (const std::size_t current_index : ordered_indices) {
        const A1Range& current = ranges[current_index];
        while (!active_by_last_row.empty()
            && active_by_last_row.begin()->first < current.first.row) {
            const std::size_t expired_index = active_by_last_row.begin()->second;
            const auto active_column = active_by_first_column.find(
                ranges[expired_index].first.column);
            if (active_column == active_by_first_column.end()
                || active_column->second != expired_index) {
                throw FastXlsxError("worksheet merged-cell overlap audit lost an active range");
            }
            active_by_first_column.erase(active_column);
            active_by_last_row.erase(active_by_last_row.begin());
        }

        const auto next = active_by_first_column.lower_bound(current.first.column);
        if (next != active_by_first_column.end()
            && ranges[next->second].first.column <= current.last.column) {
            throw FastXlsxError("worksheet merged-cell ranges overlap or duplicate");
        }
        if (next != active_by_first_column.begin()) {
            const auto previous = std::prev(next);
            if (ranges[previous->second].last.column >= current.first.column) {
                throw FastXlsxError("worksheet merged-cell ranges overlap or duplicate");
            }
        }
        active_by_first_column.emplace(current.first.column, current_index);
        active_by_last_row.emplace(current.last.row, current_index);
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

std::optional<int> sheet_view_child_rank(std::string_view name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"pane", 1}, {"selection", 2}, {"pivotSelection", 3}, {"extLst", 4},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [name](const auto& value) { return value.first == name; });
    return found == std::end(ranks) ? std::nullopt
                                    : std::optional<int>(found->second);
}

enum class FrameRole {
    Generic,
    SheetViews,
    PrimarySheetView,
    OtherSheetView,
    PrimaryPane,
    OtherPane,
    PrimarySelection,
    OtherSelection,
    AutoFilter,
    MergeCells,
    MergeCell,
};

struct Frame {
    std::string local_name;
    std::string prefix;
    FrameRole role = FrameRole::Generic;
    int last_child_rank = 0;
};

bool is_target_container(FrameRole role) noexcept
{
    return role == FrameRole::SheetViews || role == FrameRole::PrimarySheetView
        || role == FrameRole::OtherSheetView || role == FrameRole::PrimaryPane
        || role == FrameRole::OtherPane || role == FrameRole::PrimarySelection
        || role == FrameRole::OtherSelection || role == FrameRole::AutoFilter
        || role == FrameRole::MergeCells || role == FrameRole::MergeCell;
}

bool is_xml_whitespace(std::string_view text) noexcept
{
    return std::all_of(text.begin(), text.end(), is_xml_space);
}

class WorksheetMetadataProjectionReader {
public:
    WorksheetMetadataProjectionReader(
        const WorksheetMetadataReadCallbacks& callbacks,
        WorksheetMetadataReaderOptions options)
        : callbacks_(callbacks)
        , options_(options)
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
            if (inside_cell_) {
                // Cell-local metadata (for example inline-string <is>) is
                // outside this worksheet-root projection. The event reader
                // still validates its cell boundaries for us.
                return;
            }
            consume_metadata(event);
            return;
        case WorksheetEventKind::RawText:
            if (inside_cell_) {
                return;
            }
            consume_raw_text(event.raw_xml);
            return;
        case WorksheetEventKind::Unsupported:
            if (inside_target_container()) {
                throw FastXlsxError(
                    "worksheet metadata contains unsupported non-element content");
            }
            return;
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::ProcessingInstruction:
        case WorksheetEventKind::XmlDeclaration:
        case WorksheetEventKind::RowStart:
        case WorksheetEventKind::RowEnd:
            return;
        case WorksheetEventKind::CellStart:
            inside_cell_ = true;
            return;
        case WorksheetEventKind::CellEnd:
            inside_cell_ = false;
            return;
        case WorksheetEventKind::CellValueMarkup:
        case WorksheetEventKind::CellValue:
            return;
        }
    }

    [[nodiscard]] WorksheetMetadataReadSummary finish()
    {
        if (!saw_worksheet_start_ || !saw_sheet_data_start_ || !saw_sheet_data_end_
            || !saw_worksheet_end_) {
            throw FastXlsxError(
                "worksheet metadata reader requires a worksheet root and closed sheetData");
        }
        if (!stack_.empty()) {
            throw FastXlsxError(
                "worksheet metadata ended inside an open element");
        }
        audit_merged_cell_overlaps(merged_ranges_);
        summary_.peak_retained_merged_cell_count = merged_ranges_.size();
        return summary_;
    }

private:
    void consume_worksheet_start(const WorksheetEvent& event)
    {
        if (saw_worksheet_start_) {
            throw FastXlsxError("worksheet metadata contains duplicate worksheet roots");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "worksheet" || tag.self_closing) {
            throw FastXlsxError("worksheet metadata contains an invalid worksheet root");
        }
        root_prefix_ = std::string(tag.prefix);
        saw_worksheet_start_ = true;
    }

    void consume_worksheet_end(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.self_closing || !tag.closing || tag.local_name != "worksheet"
            || tag.prefix != root_prefix_) {
            throw FastXlsxError("worksheet metadata worksheet root QName is mismatched");
        }
        if (!stack_.empty()) {
            throw FastXlsxError(
                "worksheet metadata worksheet root closed with open metadata");
        }
        saw_worksheet_end_ = true;
    }

    void consume_sheet_data_start(const WorksheetEvent& event)
    {
        if (!saw_worksheet_start_ || saw_sheet_data_start_ || !stack_.empty()) {
            throw FastXlsxError("worksheet metadata contains an invalid sheetData boundary");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "sheetData" || tag.prefix != root_prefix_) {
            throw FastXlsxError("worksheet metadata sheetData QName is mismatched");
        }
        saw_sheet_data_start_ = true;
    }

    void consume_sheet_data_end(const WorksheetEvent& event)
    {
        if (!saw_sheet_data_start_ || saw_sheet_data_end_) {
            throw FastXlsxError("worksheet metadata contains a duplicate sheetData end");
        }
        if (!event.self_closing) {
            const ParsedTag tag = parse_tag(event.raw_xml);
            if (!tag.closing || tag.local_name != "sheetData"
                || tag.prefix != root_prefix_) {
                throw FastXlsxError("worksheet metadata sheetData QName is mismatched");
            }
        }
        saw_sheet_data_end_ = true;
    }

    void consume_raw_text(std::string_view text)
    {
        if (is_xml_whitespace(text)) {
            return;
        }
        if (inside_target_container()) {
            throw FastXlsxError(
                "worksheet metadata target container contains non-whitespace text");
        }
        throw FastXlsxError("worksheet metadata contains unexpected text");
    }

    bool inside_target_container() const noexcept
    {
        return std::any_of(stack_.begin(), stack_.end(), [](const Frame& frame) {
            return is_target_container(frame.role);
        });
    }

    void consume_metadata(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.local_name != event.element_name) {
            throw FastXlsxError("worksheet metadata element local name is mismatched");
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
            throw FastXlsxError("worksheet metadata contains mismatched element QName nesting");
        }

        const Frame frame = stack_.back();
        if (frame.role == FrameRole::MergeCells) {
            if (merge_declared_count_.has_value()
                && *merge_declared_count_ != merge_child_count_) {
                throw FastXlsxError(
                    "worksheet merged-cell count does not match its direct children");
            }
        }
        stack_.pop_back();
    }

    void enforce_top_level_schema(const ParsedTag& tag)
    {
        if (stack_.empty() && !saw_sheet_data_start_) {
            const std::optional<int> rank = prefix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet metadata has unsupported top-level metadata before sheetData");
            }
            if (*rank < last_prefix_rank_) {
                throw FastXlsxError(
                    "worksheet metadata prefix elements are not in schema order");
            }
            last_prefix_rank_ = *rank;
            if (tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet metadata top-level QName prefix differs from worksheet root");
            }
            return;
        }
        if (stack_.empty() && saw_sheet_data_end_) {
            const std::optional<int> rank = suffix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet metadata has unsupported top-level suffix metadata");
            }
            if (*rank < last_suffix_rank_) {
                throw FastXlsxError(
                    "worksheet metadata suffix elements are not in schema order");
            }
            last_suffix_rank_ = *rank;
            if (tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet metadata top-level QName prefix differs from worksheet root");
            }
            return;
        }
        if (stack_.empty()) {
            throw FastXlsxError("worksheet metadata appears in an invalid worksheet region");
        }
    }

    void consume_metadata_open(const ParsedTag& tag)
    {
        enforce_top_level_schema(tag);

        if (tag.local_name == "sheetViews") {
            open_sheet_views(tag);
            return;
        }
        if (tag.local_name == "sheetView") {
            open_sheet_view(tag);
            return;
        }
        if (tag.local_name == "pane") {
            open_pane(tag);
            return;
        }
        if (tag.local_name == "selection") {
            open_selection(tag);
            return;
        }
        if (tag.local_name == "pivotSelection") {
            if (!stack_.empty() && stack_.back().role == FrameRole::PrimarySheetView) {
                throw FastXlsxError(
                    "worksheet primary sheetView contains unsupported pivotSelection metadata");
            }
            open_generic_child(tag, FrameRole::Generic);
            return;
        }
        if (tag.local_name == "autoFilter") {
            open_auto_filter(tag);
            return;
        }
        if (tag.local_name == "mergeCells") {
            open_merge_cells(tag);
            return;
        }
        if (tag.local_name == "mergeCell") {
            open_merge_cell(tag);
            return;
        }

        if (!stack_.empty()) {
            validate_child_shape(tag);
        }
        open_generic_child(tag, FrameRole::Generic);
    }

    void open_sheet_views(const ParsedTag& tag)
    {
        if (!stack_.empty() || saw_sheet_views_) {
            throw FastXlsxError("worksheet contains duplicate or nested sheetViews containers");
        }
        saw_sheet_views_ = true;
        if (!tag.self_closing) {
            push_frame(tag, FrameRole::SheetViews);
        }
    }

    void open_sheet_view(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().role != FrameRole::SheetViews) {
            throw FastXlsxError("worksheet sheetView is not a direct sheetViews child");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError("worksheet sheetView QName prefix differs from sheetViews");
        }
        const std::optional<std::string_view> view_id_text =
            attribute_value(tag, "workbookViewId");
        if (!view_id_text.has_value()) {
            throw FastXlsxError("worksheet sheetView has no valid workbookViewId");
        }
        const std::optional<std::uint64_t> view_id = parse_unsigned_decimal(*view_id_text);
        if (!view_id.has_value()) {
            throw FastXlsxError("worksheet sheetView has no valid workbookViewId");
        }
        if (summary_.peak_sheet_view_count >= options_.max_sheet_view_count) {
            throw FastXlsxError("worksheet metadata exceeds max_sheet_view_count");
        }
        if (!workbook_view_ids_.emplace(*view_id).second) {
            throw FastXlsxError(
                "worksheet contains duplicate sheetView workbookViewId values");
        }
        ++summary_.peak_sheet_view_count;
        const bool primary = *view_id == 0;
        if (primary && primary_sheet_view_seen_) {
            throw FastXlsxError("worksheet contains duplicate primary sheetView elements");
        }
        if (primary) {
            primary_sheet_view_seen_ = true;
        }
        if (!tag.self_closing) {
            push_frame(tag, primary ? FrameRole::PrimarySheetView
                                    : FrameRole::OtherSheetView);
        }
    }

    void open_pane(const ParsedTag& tag)
    {
        if (stack_.empty()
            || (stack_.back().role != FrameRole::PrimarySheetView
                && stack_.back().role != FrameRole::OtherSheetView)) {
            throw FastXlsxError("worksheet pane is not a direct sheetView child");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError("worksheet pane QName prefix differs from its sheetView");
        }
        const bool primary = stack_.back().role == FrameRole::PrimarySheetView;
        advance_sheet_view_child(tag);
        if (primary) {
            if (primary_pane_seen_) {
                throw FastXlsxError("worksheet primary sheetView contains duplicate panes");
            }
            primary_pane_seen_ = true;
            const std::optional<std::string_view> state = attribute_value(tag, "state");
            if (!state.has_value() || *state != "frozen") {
                throw FastXlsxError(
                    "worksheet primary pane is not a supported frozen pane");
            }
            const std::uint32_t row_split = parse_split(tag, "ySplit",
                max_freeze_pane_row_split, "row");
            const std::uint32_t column_split = parse_split(tag, "xSplit",
                max_freeze_pane_column_split, "column");
            if (row_split == 0 && column_split == 0) {
                throw FastXlsxError("worksheet frozen pane has no non-zero split");
            }
            audit_top_left_cell(tag);
            audit_active_pane(tag, row_split, column_split);
            primary_row_split_ = row_split;
            primary_column_split_ = column_split;
            ++summary_.frozen_pane_count;
            if (callbacks_.on_frozen_pane) {
                callbacks_.on_frozen_pane(WorksheetFrozenPaneView {row_split, column_split});
            }
        }
        if (!tag.self_closing) {
            push_frame(tag, primary ? FrameRole::PrimaryPane : FrameRole::OtherPane);
        }
    }

    std::uint32_t parse_split(const ParsedTag& tag,
        std::string_view attribute_name,
        std::uint32_t maximum,
        std::string_view axis_name) const
    {
        const std::optional<std::string_view> value = attribute_value(tag, attribute_name);
        if (!value.has_value()) {
            return 0;
        }
        const std::optional<std::uint64_t> parsed = parse_unsigned_decimal(*value);
        if (!parsed.has_value() || *parsed > maximum) {
            throw FastXlsxError(
                "worksheet frozen pane has an invalid " + std::string(axis_name) + " split");
        }
        return static_cast<std::uint32_t>(*parsed);
    }

    void audit_top_left_cell(const ParsedTag& tag)
    {
        const std::optional<std::string_view> value = attribute_value(tag, "topLeftCell");
        if (!value.has_value()) {
            return;
        }
        record_reference_size(*value, "topLeftCell");
        if (!parse_a1_coordinate(*value).has_value()) {
            throw FastXlsxError("worksheet frozen pane has an invalid topLeftCell");
        }
    }

    void audit_active_pane(const ParsedTag& tag,
        std::uint32_t row_split,
        std::uint32_t column_split) const
    {
        const std::optional<std::string_view> value = attribute_value(tag, "activePane");
        if (!value.has_value()) {
            return;
        }
        if (*value != "topLeft" && *value != "topRight" && *value != "bottomLeft"
            && *value != "bottomRight") {
            throw FastXlsxError("worksheet frozen pane has an invalid activePane");
        }
        if ((*value == "topRight" && column_split == 0)
            || (*value == "bottomLeft" && row_split == 0)
            || (*value == "bottomRight" && (row_split == 0 || column_split == 0))) {
            throw FastXlsxError(
                "worksheet frozen pane activePane does not match its split axes");
        }
    }

    void open_selection(const ParsedTag& tag)
    {
        if (stack_.empty()
            || (stack_.back().role != FrameRole::PrimarySheetView
                && stack_.back().role != FrameRole::OtherSheetView)) {
            throw FastXlsxError("worksheet selection is not a direct sheetView child");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError("worksheet selection QName prefix differs from its sheetView");
        }
        const bool primary = stack_.back().role == FrameRole::PrimarySheetView;
        advance_sheet_view_child(tag);
        if (primary) {
            if (const std::optional<std::string_view> pane = attribute_value(tag, "pane")) {
                audit_selection_pane(*pane);
            }
        }
        if (!tag.self_closing) {
            push_frame(tag, primary ? FrameRole::PrimarySelection
                                    : FrameRole::OtherSelection);
        }
    }

    void audit_selection_pane(std::string_view pane) const
    {
        if (pane != "topLeft" && pane != "topRight" && pane != "bottomLeft"
            && pane != "bottomRight") {
            throw FastXlsxError("worksheet selection references an invalid pane");
        }
        if (!primary_pane_seen_) {
            return;
        }
        const std::uint32_t row_split = primary_row_split_.value_or(0);
        const std::uint32_t column_split = primary_column_split_.value_or(0);
        if ((pane == "topRight" && column_split == 0)
            || (pane == "bottomLeft" && row_split == 0)
            || (pane == "bottomRight" && (row_split == 0 || column_split == 0))) {
            throw FastXlsxError("worksheet selection references an unavailable pane");
        }
    }

    void open_auto_filter(const ParsedTag& tag)
    {
        if (!stack_.empty() || saw_auto_filter_) {
            throw FastXlsxError("worksheet contains duplicate or nested autoFilter elements");
        }
        const std::optional<std::string_view> reference = attribute_value(tag, "ref");
        if (!reference.has_value() || reference->empty()) {
            throw FastXlsxError("worksheet autoFilter is missing its ref");
        }
        record_reference_size(*reference, "autoFilter ref");
        const std::optional<A1Range> range = parse_a1_range(*reference);
        if (!range.has_value()) {
            throw FastXlsxError("worksheet autoFilter ref is not a valid A1 range");
        }
        saw_auto_filter_ = true;
        ++summary_.auto_filter_count;
        if (callbacks_.on_auto_filter) {
            callbacks_.on_auto_filter(WorksheetAutoFilterView {to_cell_range(*range)});
        }
        if (!tag.self_closing) {
            push_frame(tag, FrameRole::AutoFilter);
        }
    }

    void open_merge_cells(const ParsedTag& tag)
    {
        if (!stack_.empty() || saw_merge_cells_) {
            throw FastXlsxError("worksheet contains duplicate or nested mergeCells containers");
        }
        saw_merge_cells_ = true;
        merge_child_count_ = 0;
        merge_declared_count_.reset();
        if (const std::optional<std::string_view> count = attribute_value(tag, "count")) {
            const std::optional<std::uint64_t> parsed = parse_unsigned_decimal(*count);
            if (!parsed.has_value() || *parsed > options_.max_merged_cell_count) {
                throw FastXlsxError("worksheet mergeCells count exceeds max_merged_cell_count");
            }
            merge_declared_count_ = *parsed;
        }
        if (tag.self_closing) {
            if (merge_declared_count_.has_value() && *merge_declared_count_ != 0) {
                throw FastXlsxError("self-closing mergeCells container must have count zero");
            }
            return;
        }
        push_frame(tag, FrameRole::MergeCells);
    }

    void open_merge_cell(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().role != FrameRole::MergeCells) {
            throw FastXlsxError("worksheet mergeCell is not a direct mergeCells child");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError("worksheet mergeCell QName prefix differs from mergeCells");
        }
        const std::optional<std::string_view> reference = attribute_value(tag, "ref");
        if (!reference.has_value() || reference->empty()) {
            throw FastXlsxError("worksheet mergeCell is missing its ref");
        }
        record_reference_size(*reference, "mergeCell ref");
        const std::optional<A1Range> range = parse_a1_range(*reference);
        if (!range.has_value() || is_single_cell(*range)) {
            throw FastXlsxError(
                "worksheet mergeCell ref is not a valid multi-cell A1 range");
        }
        if (merge_child_count_ >= options_.max_merged_cell_count) {
            throw FastXlsxError("worksheet metadata exceeds max_merged_cell_count");
        }
        ++merge_child_count_;
        ++summary_.merged_cell_count;
        summary_.peak_retained_merged_cell_count = merged_ranges_.size() + 1U;
        merged_ranges_.push_back(*range);
        if (callbacks_.on_merged_cell) {
            callbacks_.on_merged_cell(WorksheetMergedCellView {
                merge_child_count_ - 1U, to_cell_range(*range)});
        }
        if (!tag.self_closing) {
            push_frame(tag, FrameRole::MergeCell);
        }
    }

    void validate_child_shape(const ParsedTag& tag) const
    {
        if (stack_.empty()) {
            return;
        }
        const FrameRole parent = stack_.back().role;
        if (parent == FrameRole::SheetViews) {
            throw FastXlsxError(
                "worksheet sheetViews container has an unsupported child element");
        }
        if (parent == FrameRole::MergeCells) {
            throw FastXlsxError(
                "worksheet mergeCells container has an unsupported child element");
        }
        if (parent == FrameRole::MergeCell) {
            throw FastXlsxError("worksheet mergeCell must not contain child elements");
        }
        if (parent == FrameRole::PrimaryPane || parent == FrameRole::OtherPane
            || parent == FrameRole::PrimarySelection || parent == FrameRole::OtherSelection) {
            throw FastXlsxError(
                "worksheet sheetView pane/selection must not contain child elements");
        }
        if (parent == FrameRole::PrimarySheetView || parent == FrameRole::OtherSheetView) {
            const std::optional<int> rank = sheet_view_child_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet sheetView has an unsupported child element");
            }
            if (*rank < stack_.back().last_child_rank) {
                throw FastXlsxError("worksheet sheetView children are not in schema order");
            }
        }
    }

    void open_generic_child(const ParsedTag& tag, FrameRole role)
    {
        if (!stack_.empty()
            && (stack_.back().role == FrameRole::PrimarySheetView
                || stack_.back().role == FrameRole::OtherSheetView)) {
            const std::optional<int> rank = sheet_view_child_rank(tag.local_name);
            if (rank.has_value()) {
                if (*rank < stack_.back().last_child_rank) {
                    throw FastXlsxError("worksheet sheetView children are not in schema order");
                }
                stack_.back().last_child_rank = *rank;
            }
        }
        if (!tag.self_closing) {
            push_frame(tag, role);
        }
    }

    void advance_sheet_view_child(const ParsedTag& tag)
    {
        const std::optional<int> rank = sheet_view_child_rank(tag.local_name);
        if (!rank.has_value()) {
            throw FastXlsxError("worksheet sheetView has an unsupported child element");
        }
        if (*rank < stack_.back().last_child_rank) {
            throw FastXlsxError("worksheet sheetView children are not in schema order");
        }
        stack_.back().last_child_rank = *rank;
    }

    void push_frame(const ParsedTag& tag, FrameRole role)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            throw FastXlsxError("worksheet metadata exceeds max_xml_nesting_depth");
        }
        stack_.push_back(Frame {
            std::string(tag.local_name), std::string(tag.prefix), role, 0});
        summary_.peak_xml_nesting_depth =
            std::max(summary_.peak_xml_nesting_depth, stack_.size());
    }

    void record_reference_size(std::string_view value, std::string_view label)
    {
        if (value.size() > options_.max_range_reference_bytes) {
            throw FastXlsxError(
                "worksheet metadata " + std::string(label)
                + " exceeds max_range_reference_bytes");
        }
        summary_.peak_range_reference_bytes =
            std::max(summary_.peak_range_reference_bytes, value.size());
    }

    const WorksheetMetadataReadCallbacks& callbacks_;
    WorksheetMetadataReaderOptions options_;
    WorksheetMetadataReadSummary summary_;
    std::vector<Frame> stack_;
    std::vector<A1Range> merged_ranges_;
    std::unordered_set<std::uint64_t> workbook_view_ids_;
    std::string root_prefix_;
    int last_prefix_rank_ = 0;
    int last_suffix_rank_ = 0;
    bool saw_worksheet_start_ = false;
    bool saw_sheet_data_start_ = false;
    bool saw_sheet_data_end_ = false;
    bool saw_worksheet_end_ = false;
    bool saw_sheet_views_ = false;
    bool primary_sheet_view_seen_ = false;
    bool primary_pane_seen_ = false;
    bool saw_auto_filter_ = false;
    bool saw_merge_cells_ = false;
    bool inside_cell_ = false;
    std::optional<std::uint32_t> primary_row_split_;
    std::optional<std::uint32_t> primary_column_split_;
    std::optional<std::uint64_t> merge_declared_count_;
    std::uint64_t merge_child_count_ = 0;
};

} // namespace

WorksheetMetadataReadSummary read_worksheet_metadata_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetMetadataReadCallbacks& callbacks,
    WorksheetMetadataReaderOptions options)
{
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError(
            "WorksheetMetadataReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError(
            "WorksheetMetadataReader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_range_reference_bytes == 0) {
        throw FastXlsxError(
            "WorksheetMetadataReader requires nonzero max_range_reference_bytes");
    }
    if (options.max_sheet_view_count == 0) {
        throw FastXlsxError(
            "WorksheetMetadataReader requires nonzero max_sheet_view_count");
    }
    if (options.max_merged_cell_count == 0) {
        throw FastXlsxError(
            "WorksheetMetadataReader requires nonzero max_merged_cell_count");
    }

    WorksheetMetadataProjectionReader projection(callbacks, options);
    WorksheetEventReaderOptions event_options;
    event_options.max_window_bytes = options.max_xml_window_bytes;
    event_options.copy_context_attributes = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&projection](const WorksheetEvent& event) { projection.consume(event); },
        event_options);
    return projection.finish();
}

} // namespace fastxlsx::detail
