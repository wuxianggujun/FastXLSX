#pragma once

#include "workbook_editor_pending_edits.hpp"
#include "workbook_editor_sheet_catalog.hpp"

#include <fastxlsx/detail/materialized_worksheet_session.hpp>

#include <string>
#include <string_view>

namespace fastxlsx::detail {

class PackageEditor;

enum class WorkbookEditorSheetRenameFormulaPolicy {
    AuditOnly,
    RewriteDefinedNames,
};

struct WorkbookEditorSheetRenameOptions {
    WorkbookEditorSheetRenameFormulaPolicy formula_policy =
        WorkbookEditorSheetRenameFormulaPolicy::AuditOnly;
};

struct WorkbookEditorSheetRenameResult {
    std::string old_name;
    std::string new_name;
};

void validate_workbook_editor_sheet_rename_preflight(
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    std::string_view old_name);

void record_workbook_editor_sheet_rename_state(
    WorkbookEditorSheetCatalogPlan& sheet_catalog,
    WorkbookEditorPendingSheetDataPayloads& pending_payloads,
    std::string_view old_name,
    std::string_view new_name);

[[nodiscard]] WorkbookEditorSheetRenameResult rename_workbook_editor_sheet(
    PackageEditor& editor,
    WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    WorkbookEditorPendingSheetDataPayloads& pending_payloads,
    std::string_view old_name,
    std::string new_name,
    WorkbookEditorSheetRenameOptions options = {});

} // namespace fastxlsx::detail
