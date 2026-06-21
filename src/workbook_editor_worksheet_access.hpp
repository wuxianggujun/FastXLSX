#pragma once

#include <fastxlsx/detail/materialized_worksheet_session.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

struct WorksheetEditorCellCoordinate {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

[[nodiscard]] WorksheetEditorCellCoordinate parse_worksheet_editor_a1_cell_reference(
    std::string_view cell_reference);

void validate_worksheet_editor_cell_range(const CellRange& range);

void validate_worksheet_editor_cell_coordinate(std::uint32_t row, std::uint32_t column);

[[nodiscard]] std::vector<WorksheetCellSnapshot> public_snapshots_from_materialized_cells(
    const std::vector<MaterializedCellSnapshot>& internal_snapshots);

} // namespace fastxlsx::detail
