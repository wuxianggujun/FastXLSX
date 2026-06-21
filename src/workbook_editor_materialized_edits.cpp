#include "workbook_editor_materialized_edits.hpp"

#include "package_editor.hpp"

#include <fastxlsx/workbook.hpp>

#include <string_view>

namespace fastxlsx::detail {

std::vector<std::string> workbook_editor_pending_materialized_worksheet_names(
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions)
{
    std::vector<std::string> names;
    for (const std::string& sheet_name : sheet_catalog.current_names()) {
        const MaterializedWorksheetSession* session =
            materialized_sessions.try_session(sheet_name);
        if (session != nullptr && session->dirty()) {
            names.push_back(sheet_name);
        }
    }
    return names;
}

void validate_workbook_editor_materialized_flush_targets(
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const std::vector<MaterializedWorksheetProjection>& projections)
{
    for (const MaterializedWorksheetProjection& projection : projections) {
        if (!sheet_catalog.has_current(projection.planned_name)) {
            throw FastXlsxError(
                workbook_editor_missing_planned_sheet_message(projection.planned_name));
        }
    }
}

WorkbookEditorMaterializedFlushResult
flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
    PackageEditor& editor,
    MaterializedWorksheetSessionRegistry& materialized_sessions,
    const WorkbookEditorSheetCatalogPlan& sheet_catalog)
{
    const std::vector<MaterializedWorksheetProjection> projections =
        materialized_sessions.dirty_worksheet_chunk_sources();
    validate_workbook_editor_materialized_flush_targets(sheet_catalog, projections);

    WorkbookEditorMaterializedFlushResult result;
    for (const MaterializedWorksheetProjection& projection : projections) {
        editor.replace_worksheet_part_from_chunk_source_by_name(
            projection.planned_name, projection.read_next_chunk);

        MaterializedWorksheetSession* session =
            materialized_sessions.try_session(projection.planned_name);
        if (session != nullptr) {
            session->clear_dirty();
        }
        ++result.flushed_worksheet_count;
    }
    return result;
}

} // namespace fastxlsx::detail

