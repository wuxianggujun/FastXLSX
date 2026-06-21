#pragma once

#include "workbook_editor_pending_edits.hpp"
#include "workbook_editor_sheet_catalog.hpp"

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/detail/materialized_worksheet_session.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

class PackageEditor;

struct WorkbookEditorSheetDataReplacementInputDiagnostic {
    std::size_t row_count = 0;
    std::size_t cell_count = 0;
};

struct WorkbookEditorSheetDataReplacementResult {
    WorkbookEditorSheetDataReplacementInputDiagnostic input;
    std::size_t replacement_cell_count = 0;
    std::size_t estimated_replacement_memory_usage = 0;
};

[[nodiscard]] WorkbookEditorSheetDataReplacementInputDiagnostic
workbook_editor_sheet_data_replacement_input_diagnostic(
    const std::vector<std::vector<CellValue>>& rows) noexcept;

[[nodiscard]] CellStore workbook_editor_sheet_data_replacement_store_from_rows(
    const std::vector<std::vector<CellValue>>& rows,
    CellStoreOptions store_options = {});

void validate_workbook_editor_sheet_data_replacement_target(
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    std::string_view sheet_name);

[[nodiscard]] WorkbookEditorSheetDataReplacementResult
replace_workbook_editor_sheet_data_from_rows(
    PackageEditor& editor,
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    WorkbookEditorPendingSheetDataPayloads& pending_payloads,
    std::string_view sheet_name,
    const std::vector<std::vector<CellValue>>& rows,
    CellStoreOptions store_options,
    WorkbookEditorSheetDataReplacementInputDiagnostic input);

} // namespace fastxlsx::detail

