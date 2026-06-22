#include "workbook_editor_sheet_rename.hpp"

#include "package_editor.hpp"

#include <fastxlsx/detail/formula_reference_audit.hpp>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

namespace {

struct MaterializedFormulaRewrite {
    std::string planned_sheet_name;
    CellPosition position;
    std::string formula_text;
};

[[nodiscard]] bool rewrites_defined_names(
    WorkbookEditorSheetRenameFormulaPolicy formula_policy) noexcept
{
    return formula_policy == WorkbookEditorSheetRenameFormulaPolicy::RewriteDefinedNames
        || formula_policy
            == WorkbookEditorSheetRenameFormulaPolicy::
                RewriteDefinedNamesAndMaterializedWorksheetFormulas;
}

[[nodiscard]] bool rewrites_materialized_worksheet_formulas(
    WorkbookEditorSheetRenameFormulaPolicy formula_policy) noexcept
{
    return formula_policy
        == WorkbookEditorSheetRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
}

[[nodiscard]] CellValue formula_cell_value_with_existing_style(
    const CellRecord& source_record, std::string formula_text)
{
    CellValue value = CellValue::formula(std::move(formula_text));
    if (source_record.style_id.has_value()) {
        value = value.with_style(*source_record.style_id);
    }
    return value;
}

[[nodiscard]] std::vector<FormulaSheetReferenceRewrite> sheet_rename_formula_rewrites(
    std::string_view old_name,
    std::string_view new_name,
    const std::optional<std::string>& source_name)
{
    std::vector<FormulaSheetReferenceRewrite> rewrites {
        FormulaSheetReferenceRewrite {
            std::string(old_name),
            std::string(new_name),
        },
    };

    if (source_name.has_value() && *source_name != old_name && *source_name != new_name) {
        rewrites.push_back(FormulaSheetReferenceRewrite {
            *source_name,
            std::string(new_name),
        });
    }
    return rewrites;
}

[[nodiscard]] std::vector<MaterializedFormulaRewrite>
collect_materialized_formula_rewrites(
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    std::span<const FormulaSheetReferenceRewrite> rewrites)
{
    std::vector<MaterializedFormulaRewrite> planned_rewrites;
    for (const auto& [_, session] : materialized_sessions.sessions()) {
        for (const auto& [position, record] : session.store().records()) {
            if (record.kind != CellValueKind::Formula) {
                continue;
            }

            std::string rewritten_formula =
                rewrite_formula_sheet_references(record.text_value, rewrites);
            if (rewritten_formula == record.text_value) {
                continue;
            }

            planned_rewrites.push_back(MaterializedFormulaRewrite {
                std::string(session.planned_name()),
                position,
                std::move(rewritten_formula),
            });
        }
    }
    return planned_rewrites;
}

void preflight_materialized_formula_rewrites(
    const MaterializedWorksheetSessionRegistry& materialized_sessions,
    const std::vector<MaterializedFormulaRewrite>& planned_rewrites)
{
    std::map<std::string, CellStore> preflight_stores;
    for (const MaterializedFormulaRewrite& rewrite : planned_rewrites) {
        auto existing_store = preflight_stores.find(rewrite.planned_sheet_name);
        if (existing_store == preflight_stores.end()) {
            const MaterializedWorksheetSession* session =
                materialized_sessions.try_session(rewrite.planned_sheet_name);
            if (session == nullptr) {
                throw FastXlsxError(
                    "materialized worksheet formula rewrite session disappeared before preflight");
            }
            existing_store = preflight_stores.emplace(
                rewrite.planned_sheet_name, session->store()).first;
        }

        const CellRecord* source_record =
            existing_store->second.try_cell(rewrite.position.row, rewrite.position.column);
        if (source_record == nullptr || source_record->kind != CellValueKind::Formula) {
            throw FastXlsxError(
                "materialized worksheet formula rewrite preflight target is not a formula cell");
        }

        existing_store->second.set_cell(
            rewrite.position.row,
            rewrite.position.column,
            formula_cell_value_with_existing_style(
                *source_record, rewrite.formula_text));
    }
}

void apply_materialized_formula_rewrites(
    MaterializedWorksheetSessionRegistry& materialized_sessions,
    const std::vector<MaterializedFormulaRewrite>& planned_rewrites)
{
    for (const MaterializedFormulaRewrite& rewrite : planned_rewrites) {
        MaterializedWorksheetSession* session =
            materialized_sessions.try_session(rewrite.planned_sheet_name);
        if (session == nullptr) {
            throw FastXlsxError(
                "materialized worksheet formula rewrite session disappeared before apply");
        }
        session->replace_formula_text(
            rewrite.position.row, rewrite.position.column, rewrite.formula_text);
    }
}

} // namespace

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
    MaterializedWorksheetSessionRegistry& materialized_sessions,
    WorkbookEditorPendingSheetDataPayloads& pending_payloads,
    std::string_view old_name,
    std::string new_name,
    WorkbookEditorSheetRenameOptions options)
{
    const std::string old_name_key(old_name);
    const std::string new_name_key = new_name;
    const std::optional<std::string> source_name =
        sheet_catalog.source_name_for_current(old_name_key);
    const std::vector<FormulaSheetReferenceRewrite> formula_rewrites =
        sheet_rename_formula_rewrites(old_name_key, new_name_key, source_name);

    validate_workbook_editor_sheet_rename_preflight(materialized_sessions, old_name_key);

    std::vector<MaterializedFormulaRewrite> materialized_formula_rewrites;
    if (rewrites_materialized_worksheet_formulas(options.formula_policy)) {
        materialized_formula_rewrites =
            collect_materialized_formula_rewrites(materialized_sessions, formula_rewrites);
        preflight_materialized_formula_rewrites(
            materialized_sessions, materialized_formula_rewrites);
    }

    SheetCatalogRenameOptions catalog_options;
    if (rewrites_defined_names(options.formula_policy)) {
        catalog_options.formula_policy = SheetCatalogRenameFormulaPolicy::RewriteDefinedNames;
        catalog_options.extra_formula_rewrites.assign(
            formula_rewrites.begin() + 1, formula_rewrites.end());
    }
    editor.rename_sheet_catalog_entry(
        old_name_key, std::move(new_name), ReferencePolicy {}, catalog_options);
    record_workbook_editor_sheet_rename_state(
        sheet_catalog, pending_payloads, old_name_key, new_name_key);
    apply_materialized_formula_rewrites(materialized_sessions, materialized_formula_rewrites);

    return WorkbookEditorSheetRenameResult {old_name_key, new_name_key};
}

} // namespace fastxlsx::detail
