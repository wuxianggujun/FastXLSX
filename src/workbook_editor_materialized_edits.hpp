#pragma once

#include "workbook_editor_sheet_catalog.hpp"

#include <fastxlsx/detail/materialized_worksheet_session.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace fastxlsx::detail {

class PackageEditor;

struct WorkbookEditorMaterializedFlushResult {
    std::size_t flushed_worksheet_count = 0;
};

struct WorkbookEditorMaterializedStageResult {
    std::vector<std::string> worksheet_names;
};

[[nodiscard]] std::vector<std::string> workbook_editor_pending_materialized_worksheet_names(
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions);

void validate_workbook_editor_materialized_flush_targets(
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const std::vector<MaterializedWorksheetSheetDataProjection>& projections);

[[nodiscard]] WorkbookEditorMaterializedStageResult
stage_workbook_editor_dirty_materialized_sessions_to_patch_plan(
    PackageEditor& editor,
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    const WorkbookEditorSheetCatalogPlan& sheet_catalog);

void commit_workbook_editor_materialized_stage(
    MaterializedWorksheetSessionRegistry& materialized_sessions,
    const WorkbookEditorMaterializedStageResult& stage) noexcept;

[[nodiscard]] WorkbookEditorMaterializedFlushResult
flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
    PackageEditor& editor,
    MaterializedWorksheetSessionRegistry& materialized_sessions,
    const WorkbookEditorSheetCatalogPlan& sheet_catalog);

} // namespace fastxlsx::detail
