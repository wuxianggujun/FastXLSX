#include "workbook_editor_sheet_rename.hpp"

#include "package_editor.hpp"

#include <utility>

namespace fastxlsx::detail {

void validate_workbook_editor_sheet_rename_preflight(
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    std::string_view old_name)
{
    materialized_sessions.preflight_no_materialized_session(old_name, "rename sheet");
}

void record_workbook_editor_sheet_rename_state(
    WorkbookEditorSheetCatalogPlan& sheet_catalog,
    WorkbookEditorPendingSheetDataPayloads& pending_payloads,
    std::string_view old_name,
    std::string_view new_name)
{
    sheet_catalog.record_rename(old_name, new_name);
    pending_payloads.migrate(old_name, new_name);
}

WorkbookEditorSheetRenameResult rename_workbook_editor_sheet(
    PackageEditor& editor,
    WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    WorkbookEditorPendingSheetDataPayloads& pending_payloads,
    std::string_view old_name,
    std::string new_name,
    WorkbookEditorSheetRenameOptions options)
{
    const std::string old_name_key(old_name);
    const std::string new_name_key = new_name;

    validate_workbook_editor_sheet_rename_preflight(materialized_sessions, old_name_key);

    SheetCatalogRenameOptions catalog_options;
    if (options.formula_policy == WorkbookEditorSheetRenameFormulaPolicy::RewriteDefinedNames) {
        catalog_options.formula_policy = SheetCatalogRenameFormulaPolicy::RewriteDefinedNames;
    }
    editor.rename_sheet_catalog_entry(
        old_name_key, std::move(new_name), ReferencePolicy {}, catalog_options);
    record_workbook_editor_sheet_rename_state(
        sheet_catalog, pending_payloads, old_name_key, new_name_key);

    return WorkbookEditorSheetRenameResult {old_name_key, new_name_key};
}

} // namespace fastxlsx::detail
