#include "workbook_editor_worksheet_access.hpp"

#include <fastxlsx/detail/xml.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

namespace {

bool is_ascii_digit(char ch) noexcept
{
    return ch >= '0' && ch <= '9';
}

} // namespace

WorksheetEditorCellCoordinate parse_worksheet_editor_a1_cell_reference(
    std::string_view cell_reference)
{
    constexpr std::uint32_t max_excel_rows = 1048576;
    constexpr std::uint32_t max_excel_columns = 16384;

    if (cell_reference.empty()) {
        throw FastXlsxError("WorksheetEditor cell reference is empty");
    }

    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < cell_reference.size()) {
        const char ch = cell_reference[position];
        if (ch < 'A' || ch > 'Z') {
            break;
        }
        column = column * 26U + static_cast<std::uint32_t>(ch - 'A' + 1);
        if (column > max_excel_columns) {
            throw FastXlsxError("WorksheetEditor cell reference column exceeds Excel limits");
        }
        ++position;
    }
    if (position == 0 || position >= cell_reference.size()) {
        throw FastXlsxError("WorksheetEditor cell reference is invalid");
    }
    if (cell_reference[position] == '0') {
        throw FastXlsxError("WorksheetEditor cell reference is invalid");
    }

    std::uint64_t row = 0;
    while (position < cell_reference.size() && is_ascii_digit(cell_reference[position])) {
        row = row * 10U + static_cast<std::uint32_t>(cell_reference[position] - '0');
        if (row > max_excel_rows) {
            throw FastXlsxError("WorksheetEditor cell reference row exceeds Excel limits");
        }
        ++position;
    }
    if (position != cell_reference.size() || row == 0 || column == 0) {
        throw FastXlsxError("WorksheetEditor cell reference is invalid");
    }

    return WorksheetEditorCellCoordinate {
        static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)};
}

CellRange parse_worksheet_editor_a1_cell_range(std::string_view range_reference)
{
    if (range_reference.empty()) {
        throw FastXlsxError("WorksheetEditor cell range reference is empty");
    }

    const std::size_t separator = range_reference.find(':');
    if (separator == std::string_view::npos) {
        const WorksheetEditorCellCoordinate cell =
            parse_worksheet_editor_a1_cell_reference(range_reference);
        return CellRange {cell.row, cell.column, cell.row, cell.column};
    }
    if (separator == 0 || separator + 1 == range_reference.size() ||
        range_reference.find(':', separator + 1) != std::string_view::npos) {
        throw FastXlsxError("WorksheetEditor cell range reference is invalid");
    }

    const WorksheetEditorCellCoordinate first =
        parse_worksheet_editor_a1_cell_reference(range_reference.substr(0, separator));
    const WorksheetEditorCellCoordinate last =
        parse_worksheet_editor_a1_cell_reference(range_reference.substr(separator + 1));

    const CellRange range {first.row, first.column, last.row, last.column};
    validate_worksheet_editor_cell_range(range);
    return range;
}

void validate_worksheet_editor_cell_range(const CellRange& range)
{
    (void)range_reference(range);
}

void validate_worksheet_editor_cell_coordinate(std::uint32_t row, std::uint32_t column)
{
    try {
        (void)cell_reference(row, column);
    } catch (const FastXlsxError& error) {
        throw FastXlsxError(
            "WorksheetEditor cell coordinate is invalid: " + std::string(error.what()));
    }
}

std::vector<WorksheetCellSnapshot> public_snapshots_from_materialized_cells(
    const std::vector<MaterializedCellSnapshot>& internal_snapshots)
{
    std::vector<WorksheetCellSnapshot> snapshots;
    snapshots.reserve(internal_snapshots.size());
    for (const MaterializedCellSnapshot& snapshot : internal_snapshots) {
        snapshots.push_back(WorksheetCellSnapshot {
            WorksheetCellReference {snapshot.position.row, snapshot.position.column},
            snapshot.value,
        });
    }
    return snapshots;
}

} // namespace fastxlsx::detail
