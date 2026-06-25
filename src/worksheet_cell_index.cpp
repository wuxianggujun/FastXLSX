#include <fastxlsx/detail/worksheet_cell_index.hpp>

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::uint32_t max_excel_rows = 1048576U;
constexpr std::uint32_t max_excel_columns = 16384U;
constexpr std::size_t default_materialized_index_chunk_size = 64U * 1024U;

bool is_ascii_alpha(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_ascii_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

std::uint32_t uppercase_column_value(char ch)
{
    const char upper =
        static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return static_cast<std::uint32_t>(upper - 'A' + 1);
}

struct WorksheetCellCoordinate {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

bool coordinate_less(
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& left,
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& right) noexcept
{
    if (left.row != right.row) {
        return left.row < right.row;
    }
    return left.column < right.column;
}

bool coordinate_less(
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& left,
    WorksheetCellCoordinate right) noexcept
{
    if (left.row != right.row) {
        return left.row < right.row;
    }
    return left.column < right.column;
}

bool coordinate_equal(
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& left,
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& right) noexcept
{
    return left.row == right.row && left.column == right.column;
}

bool coordinate_equal(
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& left,
    WorksheetCellCoordinate right) noexcept
{
    return left.row == right.row && left.column == right.column;
}

std::optional<WorksheetCellCoordinate> parse_cell_reference_for_lookup(
    std::string_view reference) noexcept
{
    if (reference.empty()) {
        return std::nullopt;
    }

    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < reference.size() && is_ascii_alpha(reference[position])) {
        column = column * 26U + uppercase_column_value(reference[position]);
        if (column > max_excel_columns) {
            return std::nullopt;
        }
        ++position;
    }
    if (position == 0 || position >= reference.size()) {
        return std::nullopt;
    }

    std::uint64_t row = 0;
    while (position < reference.size() && is_ascii_digit(reference[position])) {
        if (row > static_cast<std::uint64_t>(max_excel_rows) / 10U) {
            return std::nullopt;
        }
        row = row * 10U + static_cast<std::uint32_t>(reference[position] - '0');
        if (row > max_excel_rows) {
            return std::nullopt;
        }
        ++position;
    }
    if (position != reference.size() || row == 0 || column == 0) {
        return std::nullopt;
    }

    return WorksheetCellCoordinate {
        static_cast<std::uint32_t>(row),
        static_cast<std::uint32_t>(column),
    };
}

WorksheetCellCoordinate parse_source_cell_reference(std::string_view reference)
{
    if (reference.empty()) {
        throw fastxlsx::FastXlsxError("worksheet cell index requires cell r attributes");
    }

    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < reference.size() && is_ascii_alpha(reference[position])) {
        column = column * 26U + uppercase_column_value(reference[position]);
        if (column > max_excel_columns) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index source cell column exceeds Excel limits");
        }
        ++position;
    }
    if (position == 0 || position >= reference.size()) {
        throw fastxlsx::FastXlsxError(
            "worksheet cell index found an invalid source cell reference");
    }

    std::uint64_t row = 0;
    while (position < reference.size() && is_ascii_digit(reference[position])) {
        if (row > static_cast<std::uint64_t>(max_excel_rows) / 10U) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index source cell row exceeds Excel limits");
        }
        row = row * 10U + static_cast<std::uint32_t>(reference[position] - '0');
        if (row > max_excel_rows) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index source cell row exceeds Excel limits");
        }
        ++position;
    }
    if (position != reference.size() || row == 0 || column == 0) {
        throw fastxlsx::FastXlsxError(
            "worksheet cell index found an invalid source cell reference");
    }

    return WorksheetCellCoordinate {
        static_cast<std::uint32_t>(row),
        static_cast<std::uint32_t>(column),
    };
}

std::string cell_reference_from_coordinate(WorksheetCellCoordinate coordinate)
{
    std::string column_name;
    std::uint32_t column = coordinate.column;
    while (column > 0) {
        --column;
        column_name.push_back(
            static_cast<char>('A' + static_cast<char>(column % 26U)));
        column /= 26U;
    }
    std::reverse(column_name.begin(), column_name.end());
    return column_name + std::to_string(coordinate.row);
}

std::uint64_t event_end_offset(const fastxlsx::detail::WorksheetEvent& event)
{
    if (static_cast<std::uint64_t>(event.raw_xml.size())
        > std::numeric_limits<std::uint64_t>::max() - event.raw_xml_offset) {
        throw fastxlsx::FastXlsxError("worksheet cell index source offset overflow");
    }
    return event.raw_xml_offset + static_cast<std::uint64_t>(event.raw_xml.size());
}

struct ActiveCellRange {
    std::string reference;
    std::uint64_t start_offset = 0;
};

class WorksheetCellIndexBuilder {
public:
    void consume(const fastxlsx::detail::WorksheetEvent& event)
    {
        using fastxlsx::detail::WorksheetEventKind;

        if (event.kind == WorksheetEventKind::CellStart) {
            consume_cell_start(event);
            return;
        }
        if (event.kind == WorksheetEventKind::CellEnd) {
            consume_cell_end(event);
        }
    }

    [[nodiscard]] fastxlsx::detail::WorksheetCellIndex finish() &&
    {
        if (active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError("worksheet cell index ended inside a source cell");
        }
        index_.finalize();
        return std::move(index_);
    }

private:
    void consume_cell_start(const fastxlsx::detail::WorksheetEvent& event)
    {
        if (event.self_closing) {
            index_.add_cell(event.cell_reference,
                fastxlsx::detail::WorksheetCellIndexedRange {
                    event.raw_xml_offset,
                    event_end_offset(event),
                });
            return;
        }
        if (active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found a nested source cell");
        }
        active_cell_ = ActiveCellRange {
            std::string(event.cell_reference),
            event.raw_xml_offset,
        };
    }

    void consume_cell_end(const fastxlsx::detail::WorksheetEvent& event)
    {
        if (event.self_closing) {
            return;
        }
        if (!active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found a closing source cell without a start");
        }
        index_.add_cell(active_cell_->reference,
            fastxlsx::detail::WorksheetCellIndexedRange {
                active_cell_->start_offset,
                event_end_offset(event),
            });
        active_cell_.reset();
    }

    fastxlsx::detail::WorksheetCellIndex index_;
    std::optional<ActiveCellRange> active_cell_;
};

std::size_t materialized_index_chunk_size(
    fastxlsx::detail::WorksheetEventReaderOptions options)
{
    if (options.max_window_bytes == 0) {
        return 1;
    }
    return std::max<std::size_t>(
        1U, std::min(default_materialized_index_chunk_size, options.max_window_bytes));
}

} // namespace

namespace fastxlsx::detail {

WorksheetCellIndex WorksheetCellIndex::build_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    WorksheetEventReaderOptions options)
{
    WorksheetCellIndexBuilder builder;
    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&](const WorksheetEvent& event) {
            builder.consume(event);
        },
        options);
    return std::move(builder).finish();
}

WorksheetCellIndex WorksheetCellIndex::build_from_xml(
    std::string_view worksheet_xml,
    WorksheetEventReaderOptions options)
{
    const std::size_t chunk_width = materialized_index_chunk_size(options);
    std::size_t position = 0;
    WorksheetInputChunkCallback source =
        [worksheet_xml, chunk_width, position](std::string& output_chunk) mutable {
            if (position >= worksheet_xml.size()) {
                output_chunk.clear();
                return false;
            }
            const std::size_t size = std::min(chunk_width, worksheet_xml.size() - position);
            output_chunk.assign(worksheet_xml.data() + position, size);
            position += size;
            return true;
        };
    return build_from_chunk_source(source, options);
}

const WorksheetCellIndexedRange* WorksheetCellIndex::find(
    std::string_view cell_reference) const noexcept
{
    const std::optional<WorksheetCellCoordinate> coordinate =
        parse_cell_reference_for_lookup(cell_reference);
    if (!coordinate.has_value()) {
        return nullptr;
    }

    if (!cells_are_sorted_) {
        const auto cell = std::find_if(
            cells_by_position_.begin(),
            cells_by_position_.end(),
            [&](const CellEntry& entry) {
                return coordinate_equal(entry, *coordinate);
            });
        return cell == cells_by_position_.end() ? nullptr : &cell->range;
    }

    const auto cell = std::lower_bound(
        cells_by_position_.begin(),
        cells_by_position_.end(),
        *coordinate,
        [](const CellEntry& entry, WorksheetCellCoordinate target) {
            return coordinate_less(entry, target);
        });
    if (cell == cells_by_position_.end() || !coordinate_equal(*cell, *coordinate)) {
        return nullptr;
    }
    return &cell->range;
}

const WorksheetCellIndex::CellRangeMap& WorksheetCellIndex::cells() const
{
    if (cells_snapshot_valid_) {
        return cells_snapshot_;
    }

    cells_snapshot_.clear();
    for (const CellEntry& entry : cells_by_position_) {
        cells_snapshot_.emplace(
            cell_reference_from_coordinate(WorksheetCellCoordinate {
                entry.row,
                entry.column,
            }),
            entry.range);
    }
    cells_snapshot_valid_ = true;
    return cells_snapshot_;
}

void WorksheetCellIndex::add_cell(
    std::string_view cell_reference,
    WorksheetCellIndexedRange range)
{
    if (range.end_offset < range.start_offset) {
        throw FastXlsxError("worksheet cell index found an invalid source cell range");
    }

    const WorksheetCellCoordinate coordinate = parse_source_cell_reference(cell_reference);
    const CellEntry entry {
        coordinate.row,
        coordinate.column,
        range,
    };
    if (!cells_by_position_.empty()) {
        const CellEntry& previous = cells_by_position_.back();
        if (coordinate_equal(previous, entry)) {
            throw FastXlsxError(
                "worksheet cell index found duplicate source cell reference: "
                + cell_reference_from_coordinate(coordinate));
        }
        if (coordinate_less(entry, previous)) {
            cells_are_sorted_ = false;
        }
    }
    cells_by_position_.push_back(entry);
    cells_snapshot_valid_ = false;
}

void WorksheetCellIndex::finalize()
{
    if (!cells_are_sorted_) {
        std::sort(cells_by_position_.begin(),
            cells_by_position_.end(),
            [](const CellEntry& left, const CellEntry& right) {
                return coordinate_less(left, right);
            });
    }

    for (std::size_t index_position = 1; index_position < cells_by_position_.size();
         ++index_position) {
        const CellEntry& previous = cells_by_position_[index_position - 1];
        const CellEntry& current = cells_by_position_[index_position];
        if (coordinate_equal(previous, current)) {
            throw FastXlsxError(
                "worksheet cell index found duplicate source cell reference: "
                + cell_reference_from_coordinate(WorksheetCellCoordinate {
                    current.row,
                    current.column,
                }));
        }
    }

    cells_are_sorted_ = true;
    cells_snapshot_valid_ = false;
}

std::vector<WorksheetIndexedCellRewrite> plan_indexed_cell_rewrites(
    const WorksheetCellIndex& index,
    std::span<const std::string_view> cell_references)
{
    std::set<std::string, std::less<>> seen_targets;
    std::vector<WorksheetIndexedCellRewrite> plan;
    plan.reserve(cell_references.size());

    for (std::string_view cell_reference : cell_references) {
        if (cell_reference.empty()) {
            throw FastXlsxError("worksheet indexed rewrite target cell reference is empty");
        }

        auto [_, inserted] = seen_targets.emplace(cell_reference);
        if (!inserted) {
            throw FastXlsxError(
                "worksheet indexed rewrite target cell reference is duplicated: "
                + std::string(cell_reference));
        }

        const WorksheetCellIndexedRange* range = index.find(cell_reference);
        if (range == nullptr) {
            throw FastXlsxError(
                "worksheet indexed rewrite target cell is missing from source index: "
                + std::string(cell_reference));
        }

        plan.push_back(WorksheetIndexedCellRewrite {
            std::string(cell_reference),
            *range,
        });
    }

    std::sort(plan.begin(), plan.end(), [](const auto& left, const auto& right) {
        if (left.source_range.start_offset != right.source_range.start_offset) {
            return left.source_range.start_offset < right.source_range.start_offset;
        }
        return left.source_range.end_offset < right.source_range.end_offset;
    });

    for (std::size_t index_position = 1; index_position < plan.size(); ++index_position) {
        if (plan[index_position - 1].source_range.end_offset
            > plan[index_position].source_range.start_offset) {
            throw FastXlsxError(
                "worksheet indexed rewrite source cell ranges overlap");
        }
    }

    return plan;
}

std::string_view worksheet_cell_range_xml(
    std::string_view worksheet_xml,
    const WorksheetCellIndexedRange& range)
{
    if (range.end_offset < range.start_offset
        || range.end_offset > static_cast<std::uint64_t>(worksheet_xml.size())) {
        throw FastXlsxError("worksheet cell index range is outside worksheet XML");
    }
    const std::uint64_t size = range.end_offset - range.start_offset;
    return worksheet_xml.substr(
        static_cast<std::size_t>(range.start_offset),
        static_cast<std::size_t>(size));
}

} // namespace fastxlsx::detail
