#include "workbook_editor_sheet_data_replacement.hpp"

#include "package_editor.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace fastxlsx::detail {

WorkbookEditorSheetDataReplacementInputDiagnostic
workbook_editor_sheet_data_replacement_input_diagnostic(
    const std::vector<std::vector<CellValue>>& rows) noexcept
{
    WorkbookEditorSheetDataReplacementInputDiagnostic diagnostic;
    diagnostic.row_count = rows.size();
    for (const std::vector<CellValue>& row : rows) {
        diagnostic.cell_count += row.size();
    }
    return diagnostic;
}

CellStore workbook_editor_sheet_data_replacement_store_from_rows(
    const std::vector<std::vector<CellValue>>& rows,
    CellStoreOptions store_options)
{
    CellStore store(std::move(store_options));
    std::uint32_t row_index = 1;
    for (const std::vector<CellValue>& row : rows) {
        std::uint32_t column_index = 1;
        for (const CellValue& value : row) {
            store.set_cell(row_index, column_index, value);
            ++column_index;
        }
        ++row_index;
    }
    return store;
}

void validate_workbook_editor_sheet_data_replacement_target(
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    std::string_view sheet_name)
{
    if (!sheet_catalog.has_current(sheet_name)) {
        throw FastXlsxError(workbook_editor_missing_planned_sheet_message(sheet_name));
    }

    materialized_sessions.preflight_no_materialized_session(
        sheet_name, "replace sheet data");
}

WorkbookEditorSheetDataReplacementResult replace_workbook_editor_sheet_data_from_rows(
    PackageEditor& editor,
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    WorkbookEditorPendingSheetDataPayloads& pending_payloads,
    std::string_view sheet_name,
    const std::vector<std::vector<CellValue>>& rows,
    CellStoreOptions store_options,
    WorkbookEditorSheetDataReplacementInputDiagnostic input)
{
    validate_workbook_editor_sheet_data_replacement_target(
        sheet_catalog, materialized_sessions, sheet_name);

    CellStore store =
        workbook_editor_sheet_data_replacement_store_from_rows(rows, std::move(store_options));

    // The chunk source borrows the store, and PackageEditor consumes it during
    // this call before the local store goes out of scope.
    const WorksheetInputChunkCallback sheet_data_source =
        cell_store_sheet_data_chunk_source(store);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        sheet_name, sheet_data_source);

    WorkbookEditorSheetDataReplacementResult result;
    result.input = input;
    result.replacement_cell_count = store.cell_count();
    result.estimated_replacement_memory_usage = store.estimated_memory_usage();

    pending_payloads.record(std::string(sheet_name),
        result.replacement_cell_count,
        result.estimated_replacement_memory_usage);
    return result;
}

} // namespace fastxlsx::detail
