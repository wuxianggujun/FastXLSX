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
#include <map>
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
    store_options.materialization_policy = options.materialization_policy
            == WorksheetMaterializationPolicy::RejectKnownLosses
        ? CellStoreMaterializationPolicy::RejectKnownLosses
        : CellStoreMaterializationPolicy::AllowLossyProjection;
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
    std::size_t saved_public_edit_count = 0;
    detail::WorkbookEditorPendingSheetDataPayloads pending_sheet_data_payloads;
    std::map<std::string, std::map<std::string, std::size_t, std::less<>>, std::less<>>
        pending_targeted_cell_replacements;
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

    [[nodiscard]] detail::WorkbookEditorMaterializedStageResult
    stage_dirty_materialized_sessions_to_patch_plan()
    {
        return detail::stage_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, materialized_sessions, sheet_catalog);
    }

    void commit_materialized_stage(
        const detail::WorkbookEditorMaterializedStageResult& stage) noexcept
    {
        detail::commit_workbook_editor_materialized_stage(materialized_sessions, stage);
        pending_public_edit_count += stage.worksheet_names.size();
    }

    void flush_dirty_materialized_sessions_to_patch_plan()
    {
        const detail::WorkbookEditorMaterializedStageResult stage =
            stage_dirty_materialized_sessions_to_patch_plan();
        commit_materialized_stage(stage);
    }

    [[nodiscard]] bool has_unsaved_changes() const noexcept
    {
        return pending_public_edit_count != saved_public_edit_count
            || materialized_sessions.dirty_session_count() != 0;
    }

    [[nodiscard]] std::size_t unsaved_change_count() const noexcept
    {
        const std::size_t staged_changes = pending_public_edit_count >= saved_public_edit_count
            ? pending_public_edit_count - saved_public_edit_count
            : pending_public_edit_count;
        return staged_changes + materialized_sessions.dirty_session_count();
    }

    void mark_saved() noexcept
    {
        saved_public_edit_count = pending_public_edit_count;
    }

    [[nodiscard]] bool has_pending_sheet_data_payload(std::string_view sheet_name) const noexcept
    {
        return pending_sheet_data_payloads.contains(sheet_name);
    }

    [[nodiscard]] bool has_pending_targeted_cell_replacement(
        std::string_view sheet_name) const noexcept
    {
        return pending_targeted_cell_replacements.find(sheet_name)
            != pending_targeted_cell_replacements.end();
    }

    [[nodiscard]] std::vector<std::string> pending_replacement_worksheet_names() const
    {
        return pending_sheet_data_payloads.worksheet_names(current_worksheet_names());
    }

    [[nodiscard]] std::vector<std::string> pending_targeted_cell_replacement_worksheet_names()
        const
    {
        std::vector<std::string> names;
        for (const std::string& sheet_name : current_worksheet_names()) {
            if (has_pending_targeted_cell_replacement(sheet_name)) {
                names.push_back(sheet_name);
            }
        }
        return names;
    }

    [[nodiscard]] std::size_t pending_targeted_cell_replacement_count() const noexcept
    {
        std::size_t count = 0;
        for (const auto& [_, replacements] : pending_targeted_cell_replacements) {
            count += replacements.size();
        }
        return count;
    }

    [[nodiscard]] std::size_t estimated_pending_targeted_cell_replacement_xml_bytes()
        const noexcept
    {
        std::size_t bytes = 0;
        for (const auto& [_, replacements] : pending_targeted_cell_replacements) {
            for (const auto& [__, payload_bytes] : replacements) {
                bytes += payload_bytes;
            }
        }
        return bytes;
    }

    void record_pending_targeted_cell_replacements(
        std::string_view sheet_name,
        const std::vector<std::pair<std::string, std::size_t>>& replacements)
    {
        auto& by_reference = pending_targeted_cell_replacements[std::string(sheet_name)];
        for (const auto& [cell_reference, payload_bytes] : replacements) {
            by_reference[cell_reference] = payload_bytes;
        }
    }

    void move_pending_targeted_cell_replacements(
        std::string_view old_name, std::string_view new_name)
    {
        auto source = pending_targeted_cell_replacements.find(old_name);
        if (source == pending_targeted_cell_replacements.end()) {
            return;
        }
        auto moved = std::move(source->second);
        pending_targeted_cell_replacements.erase(source);
        auto& destination = pending_targeted_cell_replacements[std::string(new_name)];
        for (auto& [cell_reference, payload_bytes] : moved) {
            destination[std::move(cell_reference)] = payload_bytes;
        }
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
            const auto pending_cell_replacements =
                pending_targeted_cell_replacements.find(current_name);
            const detail::MaterializedWorksheetSession* materialized_session =
                materialized_sessions.try_session(current_name);
            const bool sheet_data_replaced = pending_payload != nullptr;
            const bool targeted_cells_replaced =
                pending_cell_replacements != pending_targeted_cell_replacements.end();
            const bool materialized_dirty =
                materialized_session != nullptr && materialized_session->dirty();
            if (!catalog_entry.renamed && !sheet_data_replaced
                && !targeted_cells_replaced && !materialized_dirty) {
                continue;
            }

            WorkbookEditorWorksheetEditSummary summary;
            summary.source_name = catalog_entry.source_name;
            summary.planned_name = current_name;
            summary.renamed = catalog_entry.renamed;
            summary.sheet_data_replaced = sheet_data_replaced;
            summary.targeted_cells_replaced = targeted_cells_replaced;
            summary.materialized_dirty = materialized_dirty;
            if (sheet_data_replaced) {
                summary.replacement_cell_count = pending_payload->cell_count;
                summary.estimated_replacement_memory_usage =
                    pending_payload->estimated_memory_usage;
            }
            if (targeted_cells_replaced) {
                summary.targeted_cell_replacement_count =
                    pending_cell_replacements->second.size();
                for (const auto& [_, payload_bytes] :
                     pending_cell_replacements->second) {
                    summary.estimated_targeted_cell_replacement_xml_bytes +=
                        payload_bytes;
                }
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
            sheet_catalog.entries(), editor);
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
