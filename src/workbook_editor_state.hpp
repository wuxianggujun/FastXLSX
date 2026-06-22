#pragma once

#include "package_editor.hpp"
#include "workbook_editor_formula_diagnostics.hpp"
#include "workbook_editor_materialized_edits.hpp"
#include "workbook_editor_pending_edits.hpp"
#include "workbook_editor_sheet_catalog.hpp"

#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/detail/materialized_worksheet_session.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx {

namespace detail {

[[nodiscard]] inline CellStoreOptions workbook_editor_cell_store_options_from_editor_options(
    const WorkbookEditorOptions& options)
{
    CellStoreOptions store_options;
    store_options.max_cells = options.max_replacement_cells;
    store_options.memory_budget_bytes = options.replacement_memory_budget_bytes;
    return store_options;
}

[[nodiscard]] inline CellStoreOptions workbook_editor_cell_store_options_from_worksheet_options(
    const WorksheetEditorOptions& options)
{
    CellStoreOptions store_options;
    store_options.max_cells = options.max_cells;
    store_options.memory_budget_bytes = options.memory_budget_bytes;
    return store_options;
}

[[nodiscard]] inline std::vector<std::string>
workbook_editor_source_sheet_names_from_workbook_sheets(
    const std::vector<WorkbookSheetReference>& sheets)
{
    std::vector<std::string> names;
    names.reserve(sheets.size());
    for (const WorkbookSheetReference& sheet : sheets) {
        names.push_back(sheet.name);
    }
    return names;
}

[[nodiscard]] inline std::vector<WorkbookEditorWorksheetCatalogEntry>
workbook_editor_public_catalog_from_detail_catalog(
    const std::vector<WorkbookEditorSheetCatalogEntry>& detail_catalog)
{
    std::vector<WorkbookEditorWorksheetCatalogEntry> public_catalog;
    public_catalog.reserve(detail_catalog.size());
    for (const WorkbookEditorSheetCatalogEntry& entry : detail_catalog) {
        public_catalog.push_back(WorkbookEditorWorksheetCatalogEntry {
            entry.source_name,
            entry.planned_name,
            entry.renamed,
        });
    }
    return public_catalog;
}

} // namespace detail

struct WorkbookEditor::Impl {
    Impl(detail::PackageEditor editor, WorkbookEditorOptions options)
        : editor(std::move(editor))
        , options(std::move(options))
        , sheet_catalog(detail::workbook_editor_source_sheet_names_from_workbook_sheets(
              this->editor.reader().workbook_sheets()))
    {
    }

    detail::PackageEditor editor;
    WorkbookEditorOptions options;
    detail::WorkbookEditorSheetCatalogPlan sheet_catalog;
    detail::MaterializedWorksheetSessionRegistry materialized_sessions;
    std::size_t pending_public_edit_count = 0;
    detail::WorkbookEditorPendingSheetDataPayloads pending_sheet_data_payloads;
    std::optional<std::string> last_public_edit_error;

    [[nodiscard]] std::vector<std::string> source_worksheet_names() const
    {
        return sheet_catalog.source_names();
    }

    [[nodiscard]] std::vector<std::string> current_worksheet_names() const
    {
        return sheet_catalog.current_names();
    }

    [[nodiscard]] std::vector<WorkbookEditorWorksheetCatalogEntry> worksheet_catalog() const
    {
        return detail::workbook_editor_public_catalog_from_detail_catalog(
            sheet_catalog.entries());
    }

    [[nodiscard]] bool has_source_worksheet(std::string_view sheet_name) const
    {
        return sheet_catalog.has_source(sheet_name);
    }

    [[nodiscard]] bool has_current_worksheet(std::string_view sheet_name) const
    {
        return sheet_catalog.has_current(sheet_name);
    }

    [[nodiscard]] std::optional<std::string> source_name_for_current_worksheet(
        std::string_view sheet_name) const
    {
        return sheet_catalog.source_name_for_current(sheet_name);
    }

    void flush_dirty_materialized_sessions_to_patch_plan()
    {
        const detail::WorkbookEditorMaterializedFlushResult result =
            detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
                editor, materialized_sessions, sheet_catalog);
        pending_public_edit_count += result.flushed_worksheet_count;
    }

    [[nodiscard]] bool has_pending_sheet_data_payload(std::string_view sheet_name) const noexcept
    {
        return pending_sheet_data_payloads.contains(sheet_name);
    }

    [[nodiscard]] std::vector<std::string> pending_replacement_worksheet_names() const
    {
        return pending_sheet_data_payloads.worksheet_names(current_worksheet_names());
    }

    [[nodiscard]] std::vector<std::string> pending_materialized_worksheet_names() const
    {
        return detail::workbook_editor_pending_materialized_worksheet_names(
            sheet_catalog, materialized_sessions);
    }

    [[nodiscard]] std::size_t pending_materialized_cell_count() const noexcept
    {
        return materialized_sessions.dirty_cell_count();
    }

    [[nodiscard]] std::size_t estimated_pending_materialized_memory_usage() const noexcept
    {
        return materialized_sessions.estimated_dirty_memory_usage();
    }

    [[nodiscard]] std::vector<WorkbookEditorWorksheetEditSummary> pending_worksheet_edits()
        const
    {
        std::vector<WorkbookEditorWorksheetEditSummary> summaries;

        for (const detail::WorkbookEditorSheetCatalogEntry& catalog_entry :
             sheet_catalog.entries()) {
            const std::string& current_name = catalog_entry.planned_name;
            const detail::WorkbookEditorPendingSheetDataPayloadDiagnostic* pending_payload =
                pending_sheet_data_payloads.find(current_name);
            const detail::MaterializedWorksheetSession* materialized_session =
                materialized_sessions.try_session(current_name);
            const bool sheet_data_replaced = pending_payload != nullptr;
            const bool materialized_dirty =
                materialized_session != nullptr && materialized_session->dirty();
            if (!catalog_entry.renamed && !sheet_data_replaced && !materialized_dirty) {
                continue;
            }

            WorkbookEditorWorksheetEditSummary summary;
            summary.source_name = catalog_entry.source_name;
            summary.planned_name = current_name;
            summary.renamed = catalog_entry.renamed;
            summary.sheet_data_replaced = sheet_data_replaced;
            summary.materialized_dirty = materialized_dirty;
            if (sheet_data_replaced) {
                summary.replacement_cell_count = pending_payload->cell_count;
                summary.estimated_replacement_memory_usage =
                    pending_payload->estimated_memory_usage;
            }
            if (materialized_dirty) {
                summary.materialized_cell_count = materialized_session->cell_count();
                summary.estimated_materialized_memory_usage =
                    materialized_session->estimated_memory_usage();
            }
            summaries.push_back(std::move(summary));
        }

        return summaries;
    }

    [[nodiscard]] std::vector<WorkbookEditorFormulaReferenceAudit> formula_reference_audits()
        const
    {
        return detail::workbook_editor_formula_reference_audits(
            sheet_catalog.entries(), materialized_sessions);
    }

    [[nodiscard]] std::vector<WorkbookEditorFormulaReferenceAudit>
    source_formula_reference_audits() const
    {
        return detail::workbook_editor_source_formula_reference_audits(
            sheet_catalog.entries(), editor.reader());
    }

    [[nodiscard]] std::vector<WorkbookEditorDefinedNameFormulaReferenceAudit>
    defined_name_formula_reference_audits() const
    {
        return detail::workbook_editor_defined_name_formula_reference_audits(
            sheet_catalog.entries(), editor.reader());
    }

    void clear_last_edit_error()
    {
        last_public_edit_error.reset();
    }

    void record_last_edit_error(const FastXlsxError& error)
    {
        last_public_edit_error = error.what();
    }
};

} // namespace fastxlsx
