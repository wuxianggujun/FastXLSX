#pragma once

#include "test_workbook_editor_public_state_support.hpp"

namespace {

const fastxlsx::WorkbookEditorFormulaReferenceAudit* find_public_state_formula_audit(
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view qualified_reference_text)
{
    for (const fastxlsx::WorkbookEditorFormulaReferenceAudit& audit : audits) {
        if (audit.formula_cell.row == row && audit.formula_cell.column == column &&
            audit.qualified_reference_text == qualified_reference_text) {
            return &audit;
        }
    }
    return nullptr;
}

void check_public_state_renamed_shift_formula_audit(
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view expected_formula,
    std::string_view qualified_reference_text,
    std::string_view reference_text,
    std::string_view message_prefix)
{
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
        find_public_state_formula_audit(
            audits, row, column, qualified_reference_text);
    check(audit != nullptr,
        std::string(message_prefix) + " should expose the shifted audit entry");
    if (audit == nullptr) {
        return;
    }

    check(audit->formula_sheet_source_name == "Data" &&
            audit->formula_sheet_planned_name == "RenamedData",
        std::string(message_prefix) + " should report the renamed formula sheet");
    check(audit->formula_text == expected_formula &&
            audit->sheet_qualifier_text == "Data!" &&
            audit->reference_text == reference_text &&
            audit->referenced_sheet_name == "Data",
        std::string(message_prefix) + " should report shifted formula tokens");
    check(audit->matched_current_workbook_sheet &&
            audit->matched_source_sheet_name == "Data" &&
            audit->matched_planned_sheet_name == "RenamedData",
        std::string(message_prefix) + " should match the renamed workbook catalog");
    check(audit->references_renamed_source_name &&
            !audit->references_planned_sheet_name &&
            !audit->external_workbook_qualifier &&
            !audit->sheet_range_qualifier,
        std::string(message_prefix) + " should flag the stale source-name reference");
}

void check_public_state_reopened_unmatched_formula_audit(
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view expected_formula,
    std::string_view qualified_reference_text,
    std::string_view reference_text,
    std::string_view message_prefix)
{
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
        find_public_state_formula_audit(
            audits, row, column, qualified_reference_text);
    check(audit != nullptr,
        std::string(message_prefix) + " should expose the reopened audit entry");
    if (audit == nullptr) {
        return;
    }

    check(audit->formula_sheet_source_name == "RenamedData" &&
            audit->formula_sheet_planned_name == "RenamedData",
        std::string(message_prefix) + " should report the reopened formula sheet");
    check(audit->formula_text == expected_formula &&
            audit->sheet_qualifier_text == "Data!" &&
            audit->reference_text == reference_text &&
            audit->referenced_sheet_name == "Data",
        std::string(message_prefix) + " should report the reopened formula tokens");
    check(!audit->matched_current_workbook_sheet &&
            audit->matched_source_sheet_name.empty() &&
            audit->matched_planned_sheet_name.empty(),
        std::string(message_prefix) + " should leave the stale qualifier unmatched");
    check(!audit->references_renamed_source_name &&
            !audit->references_planned_sheet_name &&
            !audit->external_workbook_qualifier &&
            !audit->sheet_range_qualifier,
        std::string(message_prefix) + " should not reconstruct rename risk after reopen");
}

void check_public_state_reopened_formula_audit_clean_editor(
    const fastxlsx::WorkbookEditor& reopened,
    std::string_view message_prefix)
{
    check(!reopened.has_pending_changes() &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0 &&
            reopened.pending_replacement_cell_count() == 0 &&
            reopened.estimated_pending_replacement_memory_usage() == 0 &&
            reopened.pending_replacement_worksheet_names().empty() &&
            reopened.pending_worksheet_edits().empty() &&
            !reopened.last_edit_error().has_value(),
        std::string(message_prefix) + " should keep the reopened editor clean");
}

void check_public_state_reopened_delete_formula_audit_output(
    const std::filesystem::path& output,
    std::string_view cell_reference,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view expected_formula,
    fastxlsx::StyleId expected_style,
    std::string_view qualified_reference_text,
    std::string_view reference_text,
    std::string_view message_prefix)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") &&
            !reopened.has_worksheet("Data"),
        std::string(message_prefix) + " should expose only the saved planned sheet name");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_source_audit =
        workbook_editor_public_catalog_snapshot(reopened);
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        reopened.source_formula_reference_audits();
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_source_audit,
        std::string(message_prefix) + " source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, std::string(message_prefix) + " source audit");
    check(source_audits.size() == 1,
        std::string(message_prefix) + " source audit should keep only the surviving reference");
    check(find_public_state_formula_audit(source_audits, row, column, "Data!#REF!") == nullptr,
        std::string(message_prefix) + " source audit should skip Data!#REF!");
    check_public_state_reopened_unmatched_formula_audit(
        source_audits, row, column, expected_formula,
        qualified_reference_text, reference_text,
        std::string(message_prefix) + " source audit");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        std::string(message_prefix) + " should reopen into a clean materialized session");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, std::string(message_prefix) + " before materialized audit");

    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell(cell_reference);
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula &&
            reopened_formula->has_style() &&
            reopened_formula->style_id().value() == expected_style.value(),
        std::string(message_prefix) + " should rematerialize the styled formula");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, std::string(message_prefix) + " after formula readback");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_materialized_audit =
        workbook_editor_public_catalog_snapshot(reopened);
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> reopened_audits =
        reopened.formula_reference_audits();
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_materialized_audit,
        std::string(message_prefix) + " materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, std::string(message_prefix) + " after materialized audit");
    check(reopened_audits.size() == 1,
        std::string(message_prefix) + " should keep only the surviving reference after reopen");
    check(find_public_state_formula_audit(
              reopened_audits, row, column, "Data!#REF!") == nullptr,
        std::string(message_prefix) + " should still skip Data!#REF! after reopen");
    check_public_state_reopened_unmatched_formula_audit(
        reopened_audits, row, column, expected_formula,
        qualified_reference_text, reference_text, message_prefix);
}

void check_public_state_reopened_shift_formula_audit_output(
    const std::filesystem::path& output,
    std::string_view cell_reference,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view expected_formula,
    fastxlsx::StyleId expected_style,
    std::string_view first_qualified_reference_text,
    std::string_view first_reference_text,
    std::string_view second_qualified_reference_text,
    std::string_view second_reference_text,
    std::string_view message_prefix)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") &&
            !reopened.has_worksheet("Data"),
        std::string(message_prefix) + " should expose only the saved planned sheet name");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_source_audit =
        workbook_editor_public_catalog_snapshot(reopened);
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        reopened.source_formula_reference_audits();
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_source_audit,
        std::string(message_prefix) + " source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, std::string(message_prefix) + " source audit");
    check(source_audits.size() == 2,
        std::string(message_prefix) + " source audit should report both shifted references");
    check_public_state_reopened_unmatched_formula_audit(
        source_audits, row, column, expected_formula,
        first_qualified_reference_text, first_reference_text,
        std::string(message_prefix) + " source first audit");
    check_public_state_reopened_unmatched_formula_audit(
        source_audits, row, column, expected_formula,
        second_qualified_reference_text, second_reference_text,
        std::string(message_prefix) + " source second audit");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        std::string(message_prefix) + " should reopen into a clean materialized session");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, std::string(message_prefix) + " before materialized audit");

    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell(cell_reference);
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula &&
            reopened_formula->has_style() &&
            reopened_formula->style_id().value() == expected_style.value(),
        std::string(message_prefix) + " should rematerialize the styled formula");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, std::string(message_prefix) + " after formula readback");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_materialized_audit =
        workbook_editor_public_catalog_snapshot(reopened);
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> reopened_audits =
        reopened.formula_reference_audits();
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_materialized_audit,
        std::string(message_prefix) + " materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, std::string(message_prefix) + " after materialized audit");
    check(reopened_audits.size() == 2,
        std::string(message_prefix) + " should report both shifted references after reopen");
    check_public_state_reopened_unmatched_formula_audit(
        reopened_audits, row, column, expected_formula,
        first_qualified_reference_text, first_reference_text, message_prefix);
    check_public_state_reopened_unmatched_formula_audit(
        reopened_audits, row, column, expected_formula,
        second_qualified_reference_text, second_reference_text, message_prefix);
}

void check_public_state_source_formula_audit_preserves_shift_fixture(
    const fastxlsx::WorkbookEditor& editor,
    std::string_view message_prefix)
{
    const std::size_t pending_change_count_before_audit = editor.pending_change_count();
    const bool has_pending_changes_before_audit = editor.has_pending_changes();
    const std::vector<std::string> replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::size_t replacement_count_before_audit =
        editor.pending_replacement_cell_count();
    const std::size_t replacement_memory_before_audit =
        editor.estimated_pending_replacement_memory_usage();
    const std::vector<std::string> materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t materialized_count_before_audit =
        editor.pending_materialized_cell_count();
    const std::size_t materialized_memory_before_audit =
        editor.estimated_pending_materialized_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_audit =
        editor.pending_worksheet_edits();
    const std::vector<std::string> source_names_before_audit =
        editor.source_worksheet_names();
    const std::vector<std::string> planned_names_before_audit =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_audit =
        editor.worksheet_catalog();
    const std::optional<std::string> last_error_before_audit = editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        editor.source_formula_reference_audits();
    check(editor.pending_change_count() == pending_change_count_before_audit,
        std::string(message_prefix) + " should not increment public edit count");
    check(editor.has_pending_changes() == has_pending_changes_before_audit,
        std::string(message_prefix) + " should not change pending-change state");
    check(editor.pending_replacement_cell_count() == replacement_count_before_audit,
        std::string(message_prefix) + " should preserve replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage()
            == replacement_memory_before_audit,
        std::string(message_prefix) + " should preserve replacement memory estimate");
    check(editor.pending_replacement_worksheet_names() == replacement_names_before_audit,
        std::string(message_prefix) + " should preserve replacement worksheet names");
    check(editor.pending_materialized_worksheet_names() == materialized_names_before_audit,
        std::string(message_prefix) + " should preserve dirty materialized diagnostics");
    check(editor.pending_materialized_cell_count() == materialized_count_before_audit,
        std::string(message_prefix) + " should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage()
            == materialized_memory_before_audit,
        std::string(message_prefix) + " should preserve materialized memory estimate");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_audit),
        std::string(message_prefix) + " should preserve pending edit summaries");
    check(editor.source_worksheet_names() == source_names_before_audit,
        std::string(message_prefix) + " should preserve source worksheet names");
    check(editor.worksheet_names() == planned_names_before_audit,
        std::string(message_prefix) + " should preserve planned worksheet names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), catalog_before_audit),
        std::string(message_prefix) + " should preserve worksheet catalog");
    check(editor.last_edit_error() == last_error_before_audit,
        std::string(message_prefix) + " should not update last_edit_error");

    constexpr std::string_view source_formula = "Data!A1+Data!B1";
    check(source_audits.size() == 2,
        std::string(message_prefix) + " should report only the source formula references");
    check_public_state_renamed_shift_formula_audit(
        source_audits, 2, 4, source_formula, "Data!A1", "A1",
        std::string(message_prefix) + " source A reference");
    check_public_state_renamed_shift_formula_audit(
        source_audits, 2, 4, source_formula, "Data!B1", "B1",
        std::string(message_prefix) + " source B reference");
}

void check_public_state_delete_formula_source_audit_preserves_shift_fixture(
    const fastxlsx::WorkbookEditor& editor,
    std::string_view message_prefix)
{
    const std::size_t pending_change_count_before_audit = editor.pending_change_count();
    const bool has_pending_changes_before_audit = editor.has_pending_changes();
    const std::vector<std::string> replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::size_t replacement_count_before_audit =
        editor.pending_replacement_cell_count();
    const std::size_t replacement_memory_before_audit =
        editor.estimated_pending_replacement_memory_usage();
    const std::vector<std::string> materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t materialized_count_before_audit =
        editor.pending_materialized_cell_count();
    const std::size_t materialized_memory_before_audit =
        editor.estimated_pending_materialized_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_audit =
        editor.pending_worksheet_edits();
    const std::vector<std::string> source_names_before_audit =
        editor.source_worksheet_names();
    const std::vector<std::string> planned_names_before_audit =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_audit =
        editor.worksheet_catalog();
    const std::optional<std::string> last_error_before_audit = editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        editor.source_formula_reference_audits();
    check(editor.pending_change_count() == pending_change_count_before_audit,
        std::string(message_prefix) + " should not increment public edit count");
    check(editor.has_pending_changes() == has_pending_changes_before_audit,
        std::string(message_prefix) + " should not change pending-change state");
    check(editor.pending_replacement_cell_count() == replacement_count_before_audit,
        std::string(message_prefix) + " should preserve replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage()
            == replacement_memory_before_audit,
        std::string(message_prefix) + " should preserve replacement memory estimate");
    check(editor.pending_replacement_worksheet_names() == replacement_names_before_audit,
        std::string(message_prefix) + " should preserve replacement worksheet names");
    check(editor.pending_materialized_worksheet_names() == materialized_names_before_audit,
        std::string(message_prefix) + " should preserve dirty materialized diagnostics");
    check(editor.pending_materialized_cell_count() == materialized_count_before_audit,
        std::string(message_prefix) + " should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage()
            == materialized_memory_before_audit,
        std::string(message_prefix) + " should preserve materialized memory estimate");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_audit),
        std::string(message_prefix) + " should preserve pending edit summaries");
    check(editor.source_worksheet_names() == source_names_before_audit,
        std::string(message_prefix) + " should preserve source worksheet names");
    check(editor.worksheet_names() == planned_names_before_audit,
        std::string(message_prefix) + " should preserve planned worksheet names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), catalog_before_audit),
        std::string(message_prefix) + " should preserve worksheet catalog");
    check(editor.last_edit_error() == last_error_before_audit,
        std::string(message_prefix) + " should not update last_edit_error");

    constexpr std::string_view source_formula = "Data!A1+Data!B2";
    check(source_audits.size() == 2,
        std::string(message_prefix) + " should report only the source formula references");
    check_public_state_renamed_shift_formula_audit(
        source_audits, 2, 4, source_formula, "Data!A1", "A1",
        std::string(message_prefix) + " source A reference");
    check_public_state_renamed_shift_formula_audit(
        source_audits, 2, 4, source_formula, "Data!B2", "B2",
        std::string(message_prefix) + " source B reference");
}

std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>
check_public_state_formula_audits_preserve_editor_diagnostics(
    const fastxlsx::WorkbookEditor& editor,
    std::string_view message_prefix)
{
    const std::size_t pending_change_count_before_audit = editor.pending_change_count();
    const bool has_pending_changes_before_audit = editor.has_pending_changes();
    const std::vector<std::string> replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::size_t replacement_count_before_audit =
        editor.pending_replacement_cell_count();
    const std::size_t replacement_memory_before_audit =
        editor.estimated_pending_replacement_memory_usage();
    const std::vector<std::string> materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t materialized_count_before_audit =
        editor.pending_materialized_cell_count();
    const std::size_t materialized_memory_before_audit =
        editor.estimated_pending_materialized_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_audit =
        editor.pending_worksheet_edits();
    const std::vector<std::string> source_names_before_audit =
        editor.source_worksheet_names();
    const std::vector<std::string> planned_names_before_audit =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_audit =
        editor.worksheet_catalog();
    const std::optional<std::string> last_error_before_audit = editor.last_edit_error();

    std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        editor.formula_reference_audits();

    check(editor.pending_change_count() == pending_change_count_before_audit,
        std::string(message_prefix) + " should not increment public edit count");
    check(editor.has_pending_changes() == has_pending_changes_before_audit,
        std::string(message_prefix) + " should not change pending-change state");
    check(editor.pending_replacement_cell_count() == replacement_count_before_audit,
        std::string(message_prefix) + " should preserve replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage()
            == replacement_memory_before_audit,
        std::string(message_prefix) + " should preserve replacement memory");
    check(editor.pending_replacement_worksheet_names() == replacement_names_before_audit,
        std::string(message_prefix) + " should preserve replacement worksheet names");
    check(editor.pending_materialized_worksheet_names() == materialized_names_before_audit,
        std::string(message_prefix) + " should preserve materialized diagnostics");
    check(editor.pending_materialized_cell_count() == materialized_count_before_audit,
        std::string(message_prefix) + " should preserve materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage()
            == materialized_memory_before_audit,
        std::string(message_prefix) + " should preserve materialized memory");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_audit),
        std::string(message_prefix) + " should preserve pending edit summaries");
    check(editor.source_worksheet_names() == source_names_before_audit,
        std::string(message_prefix) + " should preserve source worksheet names");
    check(editor.worksheet_names() == planned_names_before_audit,
        std::string(message_prefix) + " should preserve planned worksheet names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), catalog_before_audit),
        std::string(message_prefix) + " should preserve worksheet catalog");
    check(editor.last_edit_error() == last_error_before_audit,
        std::string(message_prefix) + " should not update last_edit_error");

    return audits;
}

void check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        prefix + " should expose only the planned sheet name before materialization");
    check(editor.pending_change_count() == 1,
        prefix + " should count only the catalog rename before materialization");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " before materialization should not queue replacement diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should not dirty materialized diagnostics before materialization");
}

void check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        prefix + " should expose only the planned sheet name after saved reacquire");
    check(editor.pending_change_count() == 2,
        prefix + " should keep only the rename and saved materialized handoff after reacquire");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not expose replacement diagnostics after saved reacquire");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should keep materialized diagnostics empty after saved reacquire");
}

void check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        prefix + " should expose only the planned sheet name after full-calc saved reacquire");
    check(editor.pending_change_count() == 3,
        prefix + " should keep only the rename, full-calc metadata, and saved materialized handoff after reacquire");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not expose replacement diagnostics after full-calc saved reacquire");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should keep materialized diagnostics empty after full-calc saved reacquire");
}

void check_public_state_renamed_full_calc_formula_audit(
    const fastxlsx::WorkbookEditor& editor,
    std::string_view shifted_formula,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, prefix + " materialized audit");
    check(audits.size() == 2,
        prefix + " should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        audits, 3, 4, shifted_formula, "Data!A1", "A1",
        prefix + " shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        audits, 3, 4, shifted_formula, "Data!B1", "B1",
        prefix + " shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, prefix + " source audit");
}

void check_public_state_renamed_full_calc_noop_formula_audit_readback(
    const fastxlsx::WorkbookEditor& editor,
    const std::filesystem::path& noop_output,
    std::string_view shifted_formula,
    fastxlsx::StyleId styled_formula_style,
    std::string_view expected_c5_text,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check_public_state_renamed_full_calc_formula_audit(
        editor, shifted_formula, prefix + " no-op");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        prefix + " no-op output");

    fastxlsx::WorkbookEditor reopened_noop =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor reopened_noop_sheet =
        reopened_noop.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_noop_c5 =
        reopened_noop_sheet.try_cell("C5");
    check(reopened_noop_c5.has_value() &&
            reopened_noop_c5->kind() == fastxlsx::CellValueKind::Text &&
            reopened_noop_c5->text_value() == expected_c5_text,
        prefix + " no-op output should read the C5 text cell");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop, prefix + " no-op output after C5 read");
}

void check_public_state_renamed_full_calc_noop_formula_audit_source_rows_readback(
    const fastxlsx::WorkbookEditor& editor,
    const std::filesystem::path& noop_output,
    std::string_view shifted_formula,
    fastxlsx::StyleId styled_formula_style,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check_public_state_renamed_full_calc_formula_audit(
        editor, shifted_formula, prefix + " no-op");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        prefix + " no-op output");

    fastxlsx::WorkbookEditor reopened_noop =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor reopened_noop_sheet =
        reopened_noop.worksheet("RenamedData");
    check(!reopened_noop_sheet.try_cell("C5").has_value() &&
            reopened_noop_sheet.get_cell("A4").text_value() == "extra-c3",
        prefix + " no-op output should keep only shifted source rows");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop, prefix + " no-op output after source row reads");
}

void check_public_state_renamed_delete_formula_noop_audit_readback(
    const fastxlsx::WorkbookEditor& editor,
    const std::filesystem::path& noop_output,
    std::string_view cell_reference,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view expected_formula,
    fastxlsx::StyleId styled_formula_style,
    std::string_view qualified_reference_text,
    std::string_view reference_text,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> noop_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, prefix + " no-op materialized audit");
    check(noop_audits.size() == 1,
        prefix + " no-op should keep only the surviving reference");
    check(find_public_state_formula_audit(
              noop_audits, row, column, "Data!#REF!") == nullptr,
        prefix + " no-op should still skip Data!#REF!");
    check_public_state_renamed_shift_formula_audit(
        noop_audits, row, column, expected_formula,
        qualified_reference_text, reference_text,
        prefix + " no-op surviving reference");
    check_public_state_delete_formula_source_audit_preserves_shift_fixture(
        editor, prefix + " no-op source audit");
    check_public_state_reopened_delete_formula_audit_output(
        noop_output, cell_reference, row, column, expected_formula,
        styled_formula_style, qualified_reference_text, reference_text,
        prefix + " no-op output");
}

void check_public_state_renamed_delete_formula_saved_reacquire_audit(
    const fastxlsx::WorkbookEditor& editor,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view expected_formula,
    std::string_view qualified_reference_text,
    std::string_view reference_text,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> reacquired_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, prefix + " reacquire");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " reacquire should keep diagnostics clean");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, prefix);
    check(reacquired_audits.size() == 1,
        prefix + " reacquire should keep only the surviving reference");
    check(find_public_state_formula_audit(
              reacquired_audits, row, column, "Data!#REF!") == nullptr,
        prefix + " reacquire should still skip Data!#REF!");
    check_public_state_renamed_shift_formula_audit(
        reacquired_audits, row, column, expected_formula,
        qualified_reference_text, reference_text,
        prefix + " reacquire surviving reference");
    check_public_state_delete_formula_source_audit_preserves_shift_fixture(
        editor, prefix + " post-save reacquire source scan");
}

void check_public_state_renamed_insert_formula_noop_audit_readback(
    const fastxlsx::WorkbookEditor& editor,
    const std::filesystem::path& noop_output,
    std::string_view cell_reference,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view expected_formula,
    fastxlsx::StyleId styled_formula_style,
    std::string_view first_qualified_reference_text,
    std::string_view first_reference_text,
    std::string_view second_qualified_reference_text,
    std::string_view second_reference_text,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> noop_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, prefix + " no-op materialized audit");
    check(noop_audits.size() == 2,
        prefix + " no-op should keep both shifted references");
    check_public_state_renamed_shift_formula_audit(
        noop_audits, row, column, expected_formula,
        first_qualified_reference_text, first_reference_text,
        prefix + " no-op first shifted reference");
    check_public_state_renamed_shift_formula_audit(
        noop_audits, row, column, expected_formula,
        second_qualified_reference_text, second_reference_text,
        prefix + " no-op second shifted reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, prefix + " no-op source audit");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, cell_reference, row, column, expected_formula,
        styled_formula_style, first_qualified_reference_text, first_reference_text,
        second_qualified_reference_text, second_reference_text,
        prefix + " no-op output");
}

void check_public_state_renamed_insert_formula_saved_reacquire_audit(
    const fastxlsx::WorkbookEditor& editor,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view expected_formula,
    std::string_view first_qualified_reference_text,
    std::string_view first_reference_text,
    std::string_view second_qualified_reference_text,
    std::string_view second_reference_text,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, prefix);
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> post_save_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, prefix + " post-save reacquire formula audit");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " post-save reacquire formula audit should keep diagnostics clean");
    check(post_save_audits.size() == 2,
        prefix + " post-save reacquire should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        post_save_audits, row, column, expected_formula,
        first_qualified_reference_text, first_reference_text,
        prefix + " post-save reacquire first shifted reference");
    check_public_state_renamed_shift_formula_audit(
        post_save_audits, row, column, expected_formula,
        second_qualified_reference_text, second_reference_text,
        prefix + " post-save reacquire second shifted reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, prefix + " post-save reacquire source scan");
}

void check_public_state_renamed_shift_formula_audit_noop_save(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::filesystem::path& noop_output,
    const std::map<std::string, std::string>& output_entries,
    std::string_view shifted_formula,
    fastxlsx::StyleId styled_formula_style,
    std::string_view scenario)
{
    const std::string noop_scenario = std::string(scenario) + " no-op save";
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_noop =
        editor.pending_worksheet_edits();

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes(),
        noop_scenario + " should keep the materialized sheet clean");
    check(editor.pending_change_count() == 3,
        noop_scenario + " should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        noop_scenario + " should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, noop_scenario + " should not queue replacement diagnostics");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), summaries_before_noop),
        noop_scenario + " should preserve pending edit summaries");
    check(!editor.last_edit_error().has_value(),
        noop_scenario + " should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, noop_scenario);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, noop_scenario);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        noop_scenario + " output should match the first materialized output");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1", noop_scenario);
}

void check_public_state_renamed_clean_noop_save(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::filesystem::path& noop_output,
    const std::map<std::string, std::string>& output_entries,
    std::string_view scenario,
    std::size_t expected_pending_change_count,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    const std::string noop_scenario = std::string(scenario) + " no-op save";
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_noop =
        editor.pending_worksheet_edits();

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes(),
        noop_scenario + " should keep the materialized sheet clean");
    check(editor.pending_change_count() == expected_pending_change_count,
        noop_scenario + " should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        noop_scenario + " should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, noop_scenario + " should not queue replacement diagnostics");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), summaries_before_noop),
        noop_scenario + " should preserve pending edit summaries");
    check(!editor.last_edit_error().has_value(),
        noop_scenario + " should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, noop_scenario);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, noop_scenario);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        noop_scenario + " output should match the first materialized output");
    check_reopened_clean_sheet_output(
        noop_output, "RenamedData", noop_scenario, inspect);
}

std::optional<std::string> check_public_same_sheet_rename_then_replacement_guard_sequence(
    fastxlsx::WorkbookEditor& editor,
    std::string_view target_sheet_name,
    std::string_view new_sheet_name,
    std::string_view replacement_text,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);
    const std::string target = std::string(target_sheet_name);
    const std::string new_name = std::string(new_sheet_name);
    const std::string replacement = std::string(replacement_text);

    const std::optional<std::string> rename_error =
        check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet(target, new_name);
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            label + " same-sheet rename failure");

    const std::optional<std::string> replacement_error =
        check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data(
                    target, {{fastxlsx::CellValue::text(replacement)}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            label + " same-sheet replacement failure",
            PublicMaterializedGuardDiagnostic::RenameSheet);

    check(replacement_error != rename_error,
        label + " replacement guard should replace the prior rename diagnostic");

    return replacement_error;
}

std::filesystem::path write_two_sheet_source_with_styled_shift_formula(
    std::string_view name, fastxlsx::StyleId& formula_style)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    formula_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2"),
            fastxlsx::CellView::text("row2-gap-b2"),
            fastxlsx::CellView::text("row2-gap-c2"),
            fastxlsx::CellView::formula("A1+B1").with_style(formula_style)});
        data.append_row({fastxlsx::CellView::text("extra-c3")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_qualified_shift_formula(
    std::string_view name, fastxlsx::StyleId& formula_style)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    formula_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2"),
            fastxlsx::CellView::text("row2-gap-b2"),
            fastxlsx::CellView::text("row2-gap-c2"),
            fastxlsx::CellView::formula("Data!A1+Data!B1").with_style(formula_style)});
        data.append_row({fastxlsx::CellView::text("extra-c3")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_qualified_delete_formula(
    std::string_view name, fastxlsx::StyleId& formula_style)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    formula_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2"),
            fastxlsx::CellView::text("row2-gap-b2"),
            fastxlsx::CellView::text("row2-gap-c2"),
            fastxlsx::CellView::formula("Data!A1+Data!B2").with_style(formula_style)});
        data.append_row({fastxlsx::CellView::text("extra-c3")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

void check_reopened_delete_column_formula_noop_output(
    const std::filesystem::path& output,
    fastxlsx::StyleId styled_formula_style,
    std::string_view scenario)
{
    check_reopened_clean_sheet_output(
        output, "RenamedData", scenario,
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 4,
                "renamed formula delete-column no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 3,
                "renamed formula delete-column no-op output should expose shifted bounds");
            const fastxlsx::CellValue noop_a1 = noop_sheet.get_cell("A1");
            check(noop_a1.kind() == fastxlsx::CellValueKind::Number &&
                    noop_a1.number_value() == 1.0,
                "renamed formula delete-column no-op output should read shifted B1");
            const std::optional<fastxlsx::CellValue> noop_c2 =
                noop_sheet.try_cell("C2");
            check(noop_c2.has_value() &&
                    noop_c2->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_c2->text_value() == "#REF!+A1" &&
                    noop_c2->has_style() &&
                    noop_c2->style_id().value() == styled_formula_style.value(),
                "renamed formula delete-column no-op output should read translated styled formula");
            check(noop_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "renamed formula delete-column no-op output should read shifted row cells");
            check(!noop_sheet.try_cell("D2").has_value() &&
                    !noop_sheet.try_cell("A3").has_value(),
                "renamed formula delete-column no-op output should keep old coordinates absent");
        });
}

void check_reopened_delete_row_formula_noop_output(
    const std::filesystem::path& output,
    fastxlsx::StyleId styled_formula_style,
    std::string_view scenario)
{
    check_reopened_clean_sheet_output(
        output, "RenamedData", scenario,
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 5,
                "renamed formula delete-row no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 4,
                "renamed formula delete-row no-op output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_d1 =
                noop_sheet.try_cell("D1");
            check(noop_d1.has_value() &&
                    noop_d1->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_d1->text_value() == "#REF!+#REF!" &&
                    noop_d1->has_style() &&
                    noop_d1->style_id().value() == styled_formula_style.value(),
                "renamed formula delete-row no-op output should read translated styled formula");
            check(noop_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    noop_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
                    noop_sheet.get_cell("A2").text_value() == "extra-c3",
                "renamed formula delete-row no-op output should read shifted source rows");
            check(!noop_sheet.try_cell("D2").has_value() &&
                    !noop_sheet.try_cell("A3").has_value(),
                "renamed formula delete-row no-op output should keep old coordinates absent");
        });
}

void check_reopened_delete_row_formula_column_shift_noop_output(
    const std::filesystem::path& output,
    fastxlsx::StyleId styled_formula_style,
    std::string_view scenario)
{
    check_reopened_clean_sheet_output(
        output, "RenamedData", scenario,
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 5,
                "renamed formula delete-row column-shift no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 5,
                "renamed formula delete-row column-shift no-op output should expose combined shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_e1 =
                noop_sheet.try_cell("E1");
            check(noop_e1.has_value() &&
                    noop_e1->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_e1->text_value() == "#REF!+#REF!" &&
                    noop_e1->has_style() &&
                    noop_e1->style_id().value() == styled_formula_style.value(),
                "renamed formula delete-row column-shift no-op output should read translated styled formula");
            check(noop_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    noop_sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
                    noop_sheet.get_cell("A2").text_value() == "extra-c3",
                "renamed formula delete-row column-shift no-op output should read shifted source cells");
            check(!noop_sheet.try_cell("B1").has_value() &&
                    !noop_sheet.try_cell("D2").has_value() &&
                    !noop_sheet.try_cell("A3").has_value(),
                "renamed formula delete-row column-shift no-op output should keep old coordinates absent");
        });
}

void check_reopened_delete_row_formula_column_shift_snapshot_noop_output(
    const std::filesystem::path& output,
    fastxlsx::StyleId styled_formula_style,
    std::string_view scenario)
{
    check_reopened_clean_sheet_output(
        output, "RenamedData", scenario,
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 5,
                "renamed formula delete-row snapshot no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 5,
                "renamed formula delete-row snapshot no-op output should expose combined shifted bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> noop_row_one =
                noop_sheet.row_cells(1);
            check(noop_row_one.size() == 4,
                "renamed formula delete-row snapshot no-op row_cells should expose shifted row one");
            check(noop_row_one[0].reference.row == 1 &&
                    noop_row_one[0].reference.column == 1 &&
                    noop_row_one[0].value.text_value() == "placeholder-a2",
                "renamed formula delete-row snapshot no-op row_cells should read shifted A2");
            check(noop_row_one[1].reference.row == 1 &&
                    noop_row_one[1].reference.column == 3 &&
                    noop_row_one[1].value.text_value() == "row2-gap-b2",
                "renamed formula delete-row snapshot no-op row_cells should read shifted B2");
            check(noop_row_one[2].reference.row == 1 &&
                    noop_row_one[2].reference.column == 4 &&
                    noop_row_one[2].value.text_value() == "row2-gap-c2",
                "renamed formula delete-row snapshot no-op row_cells should read shifted C2");
            check(noop_row_one[3].reference.row == 1 &&
                    noop_row_one[3].reference.column == 5 &&
                    noop_row_one[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    noop_row_one[3].value.text_value() == "#REF!+#REF!" &&
                    noop_row_one[3].value.has_style() &&
                    noop_row_one[3].value.style_id().value() == styled_formula_style.value(),
                "renamed formula delete-row snapshot no-op row_cells should read translated styled formula");
            const std::vector<fastxlsx::WorksheetCellSnapshot> noop_column_five =
                noop_sheet.column_cells(5);
            check(noop_column_five.size() == 1 &&
                    noop_column_five[0].reference.row == 1 &&
                    noop_column_five[0].reference.column == 5 &&
                    noop_column_five[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    noop_column_five[0].value.text_value() == "#REF!+#REF!" &&
                    noop_column_five[0].value.has_style() &&
                    noop_column_five[0].value.style_id().value() == styled_formula_style.value(),
                "renamed formula delete-row snapshot no-op column_cells should read translated styled formula");
            check(!noop_sheet.try_cell("B1").has_value() &&
                    !noop_sheet.try_cell("D2").has_value() &&
                    !noop_sheet.try_cell("A3").has_value(),
                "renamed formula delete-row snapshot no-op output should keep old coordinates absent");
        });
}

} // namespace
