#pragma once

// Shared support for public WorkbookEditor structural shift tests.
//
// These tests build a real source workbook through the public WorkbookWriter,
// edit it through WorkbookEditor::replace_sheet_data(), and verify the output
// package through the shared ZIP test reader.
// They intentionally stay at the public-API level and do not touch the internal
// PackageEditor test surface in test_package_editor.cpp.

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>
#include <fastxlsx/streaming_writer.hpp>

#include "image_test_bytes.hpp"
#include "zip_test_utils.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, std::string_view message)
{
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAILED: %.*s\n",
            static_cast<int>(message.size()), message.data());
    }
}

void check_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    if (haystack.find(needle) == std::string::npos) {
        ++g_failures;
        std::fprintf(stderr, "FAILED: %.*s\n  missing: %.*s\n  in: %s\n",
            static_cast<int>(message.size()), message.data(),
            static_cast<int>(needle.size()), needle.data(), haystack.c_str());
    }
}

void check_not_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    check(haystack.find(needle) == std::string::npos, message);
}

void check_cell_range_equals(
    const std::optional<fastxlsx::CellRange>& range,
    std::uint32_t first_row,
    std::uint32_t first_column,
    std::uint32_t last_row,
    std::uint32_t last_column,
    std::string_view message)
{
    check(range.has_value() && range->first_row == first_row &&
            range->first_column == first_column && range->last_row == last_row &&
            range->last_column == last_column,
        message);
}

bool threw_fastxlsx_error(const std::function<void()>& action)
{
    try {
        action();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

void check_reopened_clean_sheet_output(
    const std::filesystem::path& output,
    std::string_view sheet_name,
    std::string_view scenario,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect);

void check_reopened_default_data_sheet_output(
    const std::filesystem::path& output,
    std::string_view scenario);

bool workbook_editor_catalog_entries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& lhs,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].source_name != rhs[index].source_name
            || lhs[index].planned_name != rhs[index].planned_name
            || lhs[index].renamed != rhs[index].renamed) {
            return false;
        }
    }
    return true;
}

struct WorkbookEditorPublicCatalogSnapshot {
    std::vector<std::string> source_names;
    std::vector<std::string> planned_names;
    std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog;
};

WorkbookEditorPublicCatalogSnapshot workbook_editor_public_catalog_snapshot(
    const fastxlsx::WorkbookEditor& editor)
{
    return {
        editor.source_worksheet_names(),
        editor.worksheet_names(),
        editor.worksheet_catalog(),
    };
}

void check_workbook_editor_public_catalog_preserved(
    const fastxlsx::WorkbookEditor& editor,
    const WorkbookEditorPublicCatalogSnapshot& before,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(editor.source_worksheet_names() == before.source_names,
        prefix + " should preserve source worksheet names");
    check(editor.worksheet_names() == before.planned_names,
        prefix + " should preserve planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), before.catalog),
        prefix + " should preserve worksheet catalog");
}

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

bool workbook_editor_edit_summaries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& lhs,
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& rhs);

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

bool workbook_editor_edit_summaries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& lhs,
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto& left = lhs[index];
        const auto& right = rhs[index];
        if (left.source_name != right.source_name
            || left.planned_name != right.planned_name
            || left.renamed != right.renamed
            || left.sheet_data_replaced != right.sheet_data_replaced
            || left.replacement_cell_count != right.replacement_cell_count
            || left.estimated_replacement_memory_usage
                != right.estimated_replacement_memory_usage
            || left.materialized_dirty != right.materialized_dirty
            || left.materialized_cell_count != right.materialized_cell_count
            || left.estimated_materialized_memory_usage
                != right.estimated_materialized_memory_usage) {
            return false;
        }
    }
    return true;
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

std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>
check_public_state_source_formula_audits_preserve_editor_diagnostics(
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
        editor.source_formula_reference_audits();

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

struct WorkbookEditorPublicSaveStateSnapshot {
    bool has_pending_changes{};
    std::size_t pending_change_count{};
    std::vector<std::string> pending_materialized_worksheet_names;
    std::size_t pending_materialized_cell_count{};
    std::size_t estimated_pending_materialized_memory_usage{};
    std::size_t pending_replacement_cell_count{};
    std::size_t estimated_pending_replacement_memory_usage{};
    std::vector<std::string> pending_replacement_worksheet_names;
    std::size_t pending_targeted_cell_replacement_count{};
    std::vector<std::string> pending_targeted_cell_replacement_worksheet_names;
    std::size_t estimated_pending_targeted_cell_replacement_xml_bytes{};
    std::optional<std::string> last_edit_error;
    std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> pending_worksheet_edits;
};

WorkbookEditorPublicSaveStateSnapshot workbook_editor_public_save_state_snapshot(
    const fastxlsx::WorkbookEditor& editor)
{
    return {
        editor.has_pending_changes(),
        editor.pending_change_count(),
        editor.pending_materialized_worksheet_names(),
        editor.pending_materialized_cell_count(),
        editor.estimated_pending_materialized_memory_usage(),
        editor.pending_replacement_cell_count(),
        editor.estimated_pending_replacement_memory_usage(),
        editor.pending_replacement_worksheet_names(),
        editor.pending_targeted_cell_replacement_count(),
        editor.pending_targeted_cell_replacement_worksheet_names(),
        editor.estimated_pending_targeted_cell_replacement_xml_bytes(),
        editor.last_edit_error(),
        editor.pending_worksheet_edits(),
    };
}

void check_workbook_editor_public_save_state_preserved(
    const fastxlsx::WorkbookEditor& editor,
    const WorkbookEditorPublicSaveStateSnapshot& before,
    std::string_view scenario)
{
    check(editor.has_pending_changes() == before.has_pending_changes,
        std::string(scenario) + " should preserve pending-change state");
    check(editor.pending_change_count() == before.pending_change_count,
        std::string(scenario) + " should preserve public pending change count");
    check(editor.pending_materialized_worksheet_names()
            == before.pending_materialized_worksheet_names,
        std::string(scenario) + " should preserve pending materialized worksheet names");
    check(editor.pending_materialized_cell_count()
            == before.pending_materialized_cell_count,
        std::string(scenario) + " should preserve pending materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage()
            == before.estimated_pending_materialized_memory_usage,
        std::string(scenario) + " should preserve materialized memory estimate");
    check(editor.pending_replacement_cell_count()
            == before.pending_replacement_cell_count,
        std::string(scenario) + " should preserve pending replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage()
            == before.estimated_pending_replacement_memory_usage,
        std::string(scenario) + " should preserve replacement memory estimate");
    check(editor.pending_replacement_worksheet_names()
            == before.pending_replacement_worksheet_names,
        std::string(scenario) + " should preserve pending replacement worksheet names");
    check(editor.pending_targeted_cell_replacement_count()
            == before.pending_targeted_cell_replacement_count,
        std::string(scenario) + " should preserve pending targeted cell replacement count");
    check(editor.pending_targeted_cell_replacement_worksheet_names()
            == before.pending_targeted_cell_replacement_worksheet_names,
        std::string(scenario) + " should preserve pending targeted cell worksheet names");
    check(editor.estimated_pending_targeted_cell_replacement_xml_bytes()
            == before.estimated_pending_targeted_cell_replacement_xml_bytes,
        std::string(scenario) + " should preserve targeted cell XML byte estimate");
    check(editor.last_edit_error() == before.last_edit_error,
        std::string(scenario) + " should not replace or clear last_edit_error");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), before.pending_worksheet_edits),
        std::string(scenario) + " should preserve pending worksheet edit summaries");
}

void check_workbook_editor_public_no_pending_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(!editor.has_pending_changes(), prefix + " should keep the editor clean");
    check(editor.pending_change_count() == 0,
        prefix + " should keep pending edit count empty");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should keep pending summaries empty");
}

void check_workbook_editor_no_replacement_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(editor.pending_replacement_cell_count() == 0,
        prefix + " should not expose replacement cells");
    check(editor.estimated_pending_replacement_memory_usage() == 0,
        prefix + " should not expose replacement memory");
    check(editor.pending_replacement_worksheet_names().empty(),
        prefix + " should not expose replacement sheet names");
    check(editor.pending_targeted_cell_replacement_count() == 0,
        prefix + " should not expose targeted cell patches");
    check(editor.pending_targeted_cell_replacement_worksheet_names().empty(),
        prefix + " should not expose targeted cell worksheet names");
    check(editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0,
        prefix + " should not expose targeted cell XML bytes");
    check(!editor.has_pending_targeted_cell_replacement("Data"),
        prefix + " should not report Data targeted cell patches");
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

void check_public_state_single_named_dirty_materialized_summary(
    const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::string_view worksheet_name,
    std::size_t expected_pending_change_count,
    std::string_view scenario,
    const std::optional<std::string>& expected_last_edit_error = std::nullopt)
{
    const std::string prefix = std::string(scenario);
    const std::string expected_name = std::string(worksheet_name);
    const std::size_t expected_cell_count = sheet.cell_count();
    const std::size_t expected_memory_usage = sheet.estimated_memory_usage();

    check(editor.has_pending_changes(),
        prefix + " should expose pending public state");
    check(editor.pending_change_count() == expected_pending_change_count,
        prefix + " should not count dirty materialized sessions as staged handoffs");
    check(editor.last_edit_error() == expected_last_edit_error,
        prefix + " should expose the expected last_edit_error state");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not expose replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{expected_name},
        prefix + " should expose the expected dirty materialized worksheet");
    check(editor.pending_materialized_cell_count() == expected_cell_count,
        prefix + " should expose the dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == expected_memory_usage,
        prefix + " should expose the dirty materialized memory estimate");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        prefix + " should expose one dirty materialized summary");
    if (summaries.size() == 1) {
        const auto& summary = summaries[0];
        check(summary.source_name == expected_name &&
                summary.planned_name == expected_name &&
                !summary.renamed,
            prefix + " summary should identify the worksheet without rename state");
        check(!summary.sheet_data_replaced &&
                !summary.targeted_cells_replaced &&
                summary.replacement_cell_count == 0 &&
                summary.estimated_replacement_memory_usage == 0,
            prefix + " summary should not expose replacement state");
        check(summary.materialized_dirty &&
                summary.materialized_cell_count == expected_cell_count &&
                summary.estimated_materialized_memory_usage == expected_memory_usage,
            prefix + " summary should match the dirty materialized state");
    }
}

void check_public_state_single_data_dirty_materialized_summary(
    const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_pending_change_count,
    std::string_view scenario,
    const std::optional<std::string>& expected_last_edit_error = std::nullopt)
{
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Data", expected_pending_change_count, scenario,
        expected_last_edit_error);
}

void check_public_state_renamed_dirty_materialized_summary_memory(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& first_handle,
    fastxlsx::WorksheetEditor& second_handle,
    std::size_t expected_cell_count,
    std::size_t expected_memory_usage,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(first_handle.cell_count() == expected_cell_count &&
            second_handle.cell_count() == expected_cell_count,
        prefix + " should expose the expected sparse count on both handles");
    check(first_handle.estimated_memory_usage() == expected_memory_usage &&
            second_handle.estimated_memory_usage() == expected_memory_usage,
        prefix + " should expose the expected materialized memory on both handles");
    check(editor.pending_materialized_worksheet_names() ==
            std::vector<std::string>{"RenamedData"},
        prefix + " should report the renamed sheet as dirty materialized");
    check(editor.pending_materialized_cell_count() == expected_cell_count,
        prefix + " should report the expected dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == expected_memory_usage,
        prefix + " should report the expected dirty materialized memory");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        prefix + " should expose one renamed dirty materialized summary");
    if (summaries.size() == 1) {
        const auto& summary = summaries[0];
        check(summary.source_name == "Data" &&
                summary.planned_name == "RenamedData" &&
                summary.renamed,
            prefix + " summary should report the renamed worksheet");
        check(!summary.sheet_data_replaced,
            prefix + " summary should not invent sheetData replacement");
        check(summary.replacement_cell_count == 0,
            prefix + " summary should not invent replacement cells");
        check(summary.estimated_replacement_memory_usage == 0,
            prefix + " summary should keep replacement memory empty");
        check(summary.materialized_dirty,
            prefix + " summary should report dirty materialized state");
        check(summary.materialized_cell_count == expected_cell_count,
            prefix + " summary should report the expected materialized cell count");
        check(summary.estimated_materialized_memory_usage == expected_memory_usage,
            prefix + " summary should report the expected materialized memory");
    }
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

void check_workbook_editor_no_replacement_payload_size_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(editor.pending_replacement_cell_count() == 0,
        prefix + " should not expose replacement cells");
    check(editor.estimated_pending_replacement_memory_usage() == 0,
        prefix + " should not expose replacement memory");
}

void check_workbook_editor_public_clean_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check_workbook_editor_public_no_pending_state(editor, scenario);
    check_workbook_editor_no_replacement_diagnostics(editor, scenario);
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep last_edit_error empty");
}

void check_public_inspection_preserves_last_edit_error(
    fastxlsx::WorkbookEditor& editor, const std::optional<std::string>& expected)
{
    const WorkbookEditorPublicCatalogSnapshot catalog_before =
        workbook_editor_public_catalog_snapshot(editor);
    auto check_inspection_state = [&](std::string_view api_name) {
        const std::string prefix(api_name);
        check(editor.last_edit_error() == expected,
            prefix + " should not update last_edit_error");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before, prefix);
    };

    (void)editor.worksheet_names();
    check_inspection_state("worksheet_names");

    (void)editor.has_worksheet("Data");
    check_inspection_state("has_worksheet");

    (void)editor.source_worksheet_names();
    check_inspection_state("source_worksheet_names");

    (void)editor.has_source_worksheet("Data");
    check_inspection_state("has_source_worksheet");

    (void)editor.has_pending_changes();
    check_inspection_state("has_pending_changes");

    (void)editor.pending_change_count();
    check_inspection_state("pending_change_count");

    (void)editor.pending_replacement_cell_count();
    check_inspection_state("pending_replacement_cell_count");

    (void)editor.pending_replacement_worksheet_names();
    check_inspection_state("pending_replacement_worksheet_names");

    (void)editor.pending_targeted_cell_replacement_count();
    check_inspection_state("pending_targeted_cell_replacement_count");

    (void)editor.pending_targeted_cell_replacement_worksheet_names();
    check_inspection_state("pending_targeted_cell_replacement_worksheet_names");

    (void)editor.pending_materialized_worksheet_names();
    check_inspection_state("pending_materialized_worksheet_names");

    (void)editor.pending_materialized_cell_count();
    check_inspection_state("pending_materialized_cell_count");

    (void)editor.estimated_pending_materialized_memory_usage();
    check_inspection_state("estimated_pending_materialized_memory_usage");

    (void)editor.has_pending_replacement("Data");
    check_inspection_state("has_pending_replacement");

    (void)editor.estimated_pending_replacement_memory_usage();
    check_inspection_state("estimated_pending_replacement_memory_usage");

    (void)editor.has_pending_targeted_cell_replacement("Data");
    check_inspection_state("has_pending_targeted_cell_replacement");

    (void)editor.estimated_pending_targeted_cell_replacement_xml_bytes();
    check_inspection_state("estimated_pending_targeted_cell_replacement_xml_bytes");

    (void)editor.pending_worksheet_edits();
    check_inspection_state("pending_worksheet_edits");

    (void)editor.worksheet_catalog();
    check_inspection_state("worksheet_catalog");

    (void)editor.formula_reference_audits();
    check_inspection_state("formula_reference_audits");

    (void)editor.source_formula_reference_audits();
    check_inspection_state("source_formula_reference_audits");

    (void)editor.defined_name_formula_reference_audits();
    check_inspection_state("defined_name_formula_reference_audits");
}

void check_public_materialization_failure_clean_state(
    const fastxlsx::WorkbookEditor& editor,
    const std::vector<std::string>& expected_source_names,
    const std::vector<std::string>& expected_planned_names,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& expected_catalog,
    std::string_view scenario,
    std::string_view stage,
    std::string_view recovery_sheet_name,
    const std::optional<std::string>& expected_last_error = std::nullopt)
{
    const std::string prefix = std::string(scenario) + " " + std::string(stage);

    check_workbook_editor_public_no_pending_state(editor, prefix);
    check_workbook_editor_no_replacement_diagnostics(editor, prefix);
    check(!editor.has_pending_replacement("Data"),
        prefix + " should not report a Data replacement");
    check(!editor.has_pending_replacement(recovery_sheet_name),
        prefix + " should not report a recovery-sheet replacement");
    check(editor.pending_materialized_worksheet_names().empty(),
        prefix + " should not leave materialized sessions");
    check(editor.pending_materialized_cell_count() == 0,
        prefix + " should not retain materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should not retain materialized memory estimates");
    check(editor.source_worksheet_names() == expected_source_names,
        prefix + " should preserve source worksheet_names");
    check(editor.worksheet_names() == expected_planned_names,
        prefix + " should preserve planned worksheet_names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), expected_catalog),
        prefix + " should preserve worksheet_catalog");
    check(editor.last_edit_error() == expected_last_error,
        prefix + " should preserve last_edit_error");
}

void check_public_saved_materialized_recovery_clean_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& saved_handle,
    fastxlsx::WorksheetEditor& reacquired_handle,
    const std::vector<std::string>& expected_source_names,
    const std::vector<std::string>& expected_planned_names,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& expected_catalog,
    std::string_view expected_saved_text,
    std::string_view transient_sheet_name,
    std::string_view scenario,
    std::size_t expected_pending_change_count,
    const std::optional<std::string>& expected_last_error = std::nullopt)
{
    const std::string prefix = std::string(scenario);

    check(editor.last_edit_error() == expected_last_error,
        prefix + " should preserve last_edit_error");
    check(editor.has_pending_changes(),
        prefix + " should preserve prior public edit facade state");
    check(editor.pending_change_count() == expected_pending_change_count,
        prefix + " should not queue another public edit");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not invent replacement diagnostics");
    check(!editor.has_pending_replacement("Data"),
        prefix + " should not report a Data replacement");
    check(!editor.has_pending_replacement(transient_sheet_name),
        prefix + " should not revive transient replacement state");
    check(!editor.has_pending_replacement("Missing"),
        prefix + " should not report unrelated missing replacements");
    check(editor.pending_materialized_worksheet_names().empty(),
        prefix + " should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        prefix + " should keep dirty materialized cell count clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should keep dirty materialized memory clear");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should keep worksheet edit summaries empty");
    check(editor.source_worksheet_names() == expected_source_names,
        prefix + " should preserve source worksheet_names");
    check(editor.worksheet_names() == expected_planned_names,
        prefix + " should preserve planned worksheet_names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), expected_catalog),
        prefix + " should preserve worksheet_catalog");
    check(!saved_handle.has_pending_changes() && !reacquired_handle.has_pending_changes(),
        prefix + " should keep existing handles clean");

    const fastxlsx::CellValue preserved_value = reacquired_handle.get_cell(1, 1);
    check(preserved_value.kind() == fastxlsx::CellValueKind::Text &&
            preserved_value.text_value() == expected_saved_text,
        prefix + " should preserve the saved materialized value");
}

void check_public_dirty_materialized_recovery_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& first_handle,
    fastxlsx::WorksheetEditor& second_handle,
    const std::vector<std::string>& expected_source_names,
    const std::vector<std::string>& expected_planned_names,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& expected_catalog,
    std::string_view transient_sheet_name,
    std::string_view scenario,
    std::size_t expected_pending_change_count,
    std::size_t expected_cell_count,
    std::size_t expected_memory_usage)
{
    const std::string prefix = std::string(scenario);

    check(!editor.last_edit_error().has_value(),
        prefix + " should not create edit diagnostics");
    check(editor.has_pending_changes(),
        prefix + " should preserve dirty public facade state");
    check(editor.pending_change_count() == expected_pending_change_count,
        prefix + " should preserve the pre-save public edit count");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not invent replacement diagnostics");
    check(!editor.has_pending_replacement("Data"),
        prefix + " should not report a Data replacement");
    check(!editor.has_pending_replacement(transient_sheet_name),
        prefix + " should not revive transient replacement state");
    check(first_handle.has_pending_changes() && second_handle.has_pending_changes(),
        prefix + " should dirty existing handles");
    check(first_handle.cell_count() == expected_cell_count &&
            second_handle.cell_count() == expected_cell_count,
        prefix + " should expose the expected sparse cell count on both handles");
    check(first_handle.estimated_memory_usage() == expected_memory_usage &&
            second_handle.estimated_memory_usage() == expected_memory_usage,
        prefix + " should expose the expected materialized memory on both handles");

    const std::vector<std::string> materialized_names =
        editor.pending_materialized_worksheet_names();
    check(materialized_names.size() == 1 && materialized_names[0] == "Data",
        prefix + " should report the restored source name as dirty materialized");
    check(editor.pending_materialized_cell_count() == expected_cell_count,
        prefix + " should report the expected dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == expected_memory_usage,
        prefix + " should report the expected dirty materialized memory");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        prefix + " should expose one dirty materialized summary");
    if (summaries.size() == 1) {
        const auto& summary = summaries[0];
        check(summary.source_name == "Data" && summary.planned_name == "Data",
            prefix + " summary should use restored source/planned names");
        check(!summary.renamed,
            prefix + " summary should not be marked renamed");
        check(!summary.sheet_data_replaced,
            prefix + " summary should not invent sheetData replacement");
        check(summary.replacement_cell_count == 0,
            prefix + " summary should not invent replacement cells");
        check(summary.estimated_replacement_memory_usage == 0,
            prefix + " summary should keep replacement memory empty");
        check(summary.materialized_dirty,
            prefix + " summary should report dirty materialized state");
        check(summary.materialized_cell_count == expected_cell_count,
            prefix + " summary should report the expected materialized cell count");
        check(summary.estimated_materialized_memory_usage == expected_memory_usage,
            prefix + " summary should report the expected materialized memory");
    }

    check(editor.source_worksheet_names() == expected_source_names,
        prefix + " should preserve source worksheet_names");
    check(editor.worksheet_names() == expected_planned_names,
        prefix + " should preserve planned worksheet_names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), expected_catalog),
        prefix + " should preserve worksheet_catalog");
    check(editor.has_worksheet("Data") && !editor.has_worksheet(transient_sheet_name),
        prefix + " should preserve the restored planned catalog name");
}

void reject_public_two_clean_retry_query_failures(
    fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditorOptions& options,
    const fastxlsx::WorksheetEditorOptions& mismatched_options,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("Data", mismatched_options);
    }), label + " try_worksheet should reject mismatched options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Untouched", mismatched_options);
    }), label + " worksheet should reject mismatched options");
    const std::optional<fastxlsx::WorksheetEditor> missing_try =
        editor.try_worksheet("Missing", options);
    check(!missing_try.has_value(),
        label + " try_worksheet should return empty for missing sheets");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Missing", options);
    }), label + " worksheet should throw for missing sheets");
}

void check_public_two_clean_retry_clean_after_query_failures(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    fastxlsx::WorksheetEditor& data_again,
    fastxlsx::WorksheetEditor& untouched_again,
    const std::vector<std::string>& expected_names,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& expected_catalog,
    std::size_t expected_pending_count,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(!editor.last_edit_error().has_value(),
        label + " failures should keep last_edit_error clear");
    check(editor.pending_change_count() == expected_pending_count,
        label + " failures should not add handoffs");
    check(!data.has_pending_changes() && !untouched.has_pending_changes() &&
            !data_again.has_pending_changes() && !untouched_again.has_pending_changes(),
        label + " failures should keep all materialized handles clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " failures should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        label + " failures should keep dirty materialized cells clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " failures should keep dirty materialized memory clear");
    check(editor.pending_worksheet_edits().empty(),
        label + " failures should keep worksheet summaries empty");
    check(editor.source_worksheet_names() == expected_names,
        label + " failures should preserve source worksheet names");
    check(editor.worksheet_names() == expected_names,
        label + " failures should preserve planned worksheet names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), expected_catalog),
        label + " failures should preserve worksheet catalog");
}

void reject_public_two_clean_retry_invalid_reads(
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    fastxlsx::WorksheetEditor& data_again,
    fastxlsx::WorksheetEditor& untouched_again,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(threw_fastxlsx_error([&] { (void)data_again.try_cell(0, 1); }),
        label + " invalid read should reject row zero");
    check(threw_fastxlsx_error([&] { (void)data_again.get_cell(1, 0); }),
        label + " invalid read should reject column zero");
    check(threw_fastxlsx_error([&] { (void)untouched_again.try_cell(1048577, 1); }),
        label + " invalid read should reject rows beyond Excel limit");
    check(threw_fastxlsx_error([&] { (void)untouched_again.get_cell(1, 16385); }),
        label + " invalid read should reject columns beyond Excel limit");
    check(threw_fastxlsx_error([&] { (void)data.try_cell("a1"); }),
        label + " invalid A1 read should reject lowercase references");
    check(threw_fastxlsx_error([&] { (void)untouched.get_cell("XFE1"); }),
        label + " invalid A1 read should reject columns beyond Excel limit");
    check(threw_fastxlsx_error([&] {
        (void)data_again.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), label + " invalid range read should reject row zero");
    check(threw_fastxlsx_error([&] {
        (void)untouched_again.sparse_cells(fastxlsx::CellRange {2, 1, 1, 1});
    }), label + " invalid range read should reject reversed ranges");
}

void check_public_two_clean_retry_clean_after_invalid_reads(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    fastxlsx::WorksheetEditor& data_again,
    fastxlsx::WorksheetEditor& untouched_again,
    const std::vector<std::string>& expected_names,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& expected_catalog,
    std::size_t expected_pending_count,
    std::size_t data_count,
    std::size_t data_memory,
    std::size_t untouched_count,
    std::size_t untouched_memory,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(!editor.last_edit_error().has_value(),
        label + " invalid reads should keep last_edit_error clear");
    check(editor.pending_change_count() == expected_pending_count,
        label + " invalid reads should not add handoffs");
    check(!data.has_pending_changes() && !untouched.has_pending_changes() &&
            !data_again.has_pending_changes() && !untouched_again.has_pending_changes(),
        label + " invalid reads should keep all handles clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " invalid reads should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        label + " invalid reads should keep dirty materialized cells clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " invalid reads should keep dirty materialized memory clear");
    check(editor.pending_worksheet_edits().empty(),
        label + " invalid reads should keep worksheet summaries empty");
    check(editor.source_worksheet_names() == expected_names,
        label + " invalid reads should preserve source worksheet names");
    check(editor.worksheet_names() == expected_names,
        label + " invalid reads should preserve planned worksheet names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), expected_catalog),
        label + " invalid reads should preserve worksheet catalog");
    check(data_again.cell_count() == data_count &&
            data_again.estimated_memory_usage() == data_memory,
        label + " invalid reads should preserve Data sparse diagnostics");
    check(untouched_again.cell_count() == untouched_count &&
            untouched_again.estimated_memory_usage() == untouched_memory,
        label + " invalid reads should preserve Untouched sparse diagnostics");
}

fastxlsx::CellValue public_two_clean_retry_rejected_mutation_value(
    std::string_view prefix,
    std::string_view suffix)
{
    return fastxlsx::CellValue::text(std::string(prefix) + std::string(suffix));
}

void reject_public_two_clean_retry_invalid_mutations(
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    fastxlsx::WorksheetEditor& data_again,
    fastxlsx::WorksheetEditor& untouched_again,
    std::string_view rejected_prefix,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(threw_fastxlsx_error([&] {
        data_again.set_cell(0, 1,
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-row-zero"));
    }), label + " should reject row-zero set_cell");
    check(threw_fastxlsx_error([&] {
        data_again.set_cell(1, 0,
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-column-zero"));
    }), label + " should reject column-zero set_cell");
    check(threw_fastxlsx_error([&] {
        untouched_again.set_cell(1048577, 1,
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-row-overflow"));
    }), label + " should reject row-overflow set_cell");
    check(threw_fastxlsx_error([&] {
        untouched_again.set_cell(1, 16385,
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-column-overflow"));
    }), label + " should reject column-overflow set_cell");
    check(threw_fastxlsx_error([&] {
        data.set_cell("a1",
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-lowercase-a1"));
    }), label + " should reject lowercase A1 set_cell");
    check(threw_fastxlsx_error([&] {
        untouched.set_cell("XFE1",
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-a1-column-overflow"));
    }), label + " should reject A1 column-overflow set_cell");
    check(threw_fastxlsx_error([&] { data.erase_cell(0, 1); }),
        label + " should reject row-zero erase");
    check(threw_fastxlsx_error([&] { untouched.erase_cell(1, 16385); }),
        label + " should reject column-overflow erase");
}

void check_public_two_clean_retry_clean_after_invalid_mutations(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    fastxlsx::WorksheetEditor& data_again,
    fastxlsx::WorksheetEditor& untouched_again,
    const std::vector<std::string>& expected_names,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& expected_catalog,
    std::size_t expected_pending_count,
    std::size_t data_count,
    std::size_t data_memory,
    std::size_t untouched_count,
    std::size_t untouched_memory,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);
    const std::optional<std::string> invalid_mutation_error =
        editor.last_edit_error();

    check(invalid_mutation_error.has_value(),
        label + " should record invalid mutation diagnostics");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should preserve handoff count");
    check(!data.has_pending_changes() && !untouched.has_pending_changes() &&
            !data_again.has_pending_changes() && !untouched_again.has_pending_changes(),
        label + " should keep all handles clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        label + " should keep dirty materialized cells clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " should keep dirty materialized memory clear");
    check(editor.pending_worksheet_edits().empty(),
        label + " should keep worksheet summaries empty");
    check(editor.source_worksheet_names() == expected_names,
        label + " should preserve source worksheet names");
    check(editor.worksheet_names() == expected_names,
        label + " should preserve planned worksheet names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), expected_catalog),
        label + " should preserve worksheet catalog");
    check(data.cell_count() == data_count &&
            data_again.cell_count() == data_count &&
            data.estimated_memory_usage() == data_memory &&
            data_again.estimated_memory_usage() == data_memory,
        label + " should preserve Data sparse diagnostics");
    check(untouched.cell_count() == untouched_count &&
            untouched_again.cell_count() == untouched_count &&
            untouched.estimated_memory_usage() == untouched_memory &&
            untouched_again.estimated_memory_usage() == untouched_memory,
        label + " should preserve Untouched sparse diagnostics");
    check_public_inspection_preserves_last_edit_error(
        editor, invalid_mutation_error);
}

void check_public_two_clean_retry_reacquire_clean_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    fastxlsx::WorksheetEditor& data_again,
    fastxlsx::WorksheetEditor& untouched_again,
    std::size_t expected_pending_count,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(!data.has_pending_changes() && !untouched.has_pending_changes() &&
            !data_again.has_pending_changes() && !untouched_again.has_pending_changes(),
        label + " should keep all handles clean");
    check(!editor.last_edit_error().has_value(),
        label + " should keep last_edit_error clear");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " should not create dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        label + " should not create dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " should not create dirty memory estimate");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should not add handoffs");
}

void check_public_two_clean_retry_saved_value(
    fastxlsx::WorksheetEditor& sheet,
    std::size_t row,
    std::size_t column,
    std::string_view expected_text,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);
    const fastxlsx::CellValue value = sheet.get_cell(row, column);

    check(value.kind() == fastxlsx::CellValueKind::Text &&
            value.text_value() == expected_text,
        label + " should preserve the saved materialized value");
}

void check_public_two_clean_retry_single_dirty_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& dirty,
    fastxlsx::WorksheetEditor& dirty_again,
    fastxlsx::WorksheetEditor& clean,
    fastxlsx::WorksheetEditor& clean_again,
    std::string_view expected_dirty_name,
    std::size_t expected_dirty_cell_count,
    std::size_t expected_dirty_memory_usage,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(dirty.has_pending_changes() && dirty_again.has_pending_changes() &&
            !clean.has_pending_changes() && !clean_again.has_pending_changes(),
        label + " should dirty only the touched session");
    {
        const std::vector<std::string> dirty_names =
            editor.pending_materialized_worksheet_names();
        check(dirty_names == std::vector<std::string>{std::string(expected_dirty_name)},
            label + " should expose only the touched session as dirty");
    }
    check(editor.pending_materialized_cell_count() == expected_dirty_cell_count,
        label + " should report only the touched session cells");
    check(editor.estimated_pending_materialized_memory_usage() ==
            expected_dirty_memory_usage,
        label + " should report only the touched session memory");
}

void check_public_two_clean_retry_followup_save_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& dirty,
    fastxlsx::WorksheetEditor& dirty_again,
    fastxlsx::WorksheetEditor& clean,
    fastxlsx::WorksheetEditor& clean_again,
    std::size_t expected_pending_count,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(!dirty.has_pending_changes() && !dirty_again.has_pending_changes() &&
            !clean.has_pending_changes() && !clean_again.has_pending_changes(),
        label + " should flush all materialized handles");
    check(!editor.last_edit_error().has_value(),
        label + " should keep diagnostics clear");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should queue the expected materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        label + " should clear dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " should clear dirty memory estimate");
}

std::size_t check_public_two_clean_retry_two_handle_save_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    std::size_t expected_pending_count,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(!data.has_pending_changes() && !untouched.has_pending_changes(),
        label + " should flush both materialized handles");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should queue the expected materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        label + " should clear dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " should clear dirty memory estimate");
    return editor.pending_change_count();
}

template <typename Action>
void check_public_two_clean_retry_failed_save_dirty_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    std::size_t expected_pending_count,
    Action&& action,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(threw_fastxlsx_error(std::forward<Action>(action)),
        label + " should fail");
    check(data.has_pending_changes() && untouched.has_pending_changes(),
        label + " should preserve dirty handles");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should preserve pending change count");
}

void check_internal_materialized_session_save_state(
    const fastxlsx::WorkbookEditor& editor,
    std::size_t expected_dirty_session_count,
    std::size_t expected_pending_count,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(
              editor) == expected_dirty_session_count,
        label + " should preserve dirty materialized session count");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should preserve pending change count");
}

std::size_t check_public_two_clean_two_handle_clean_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    std::size_t expected_pending_count,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(!data.has_pending_changes() && !untouched.has_pending_changes(),
        label + " should keep both handles clean");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should expose the expected materialized handoff count");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        label + " should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " should keep dirty materialized memory empty");
    return editor.pending_change_count();
}

void check_public_two_clean_preserved_clean_handles_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    std::size_t data_cell_count,
    std::size_t data_memory,
    std::size_t untouched_cell_count,
    std::size_t untouched_memory,
    std::size_t expected_pending_count,
    bool expect_editor_clean,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check_public_two_clean_two_handle_clean_state(
        editor, data, untouched, expected_pending_count, label);
    if (expect_editor_clean) {
        check(!editor.has_pending_changes(), label + " should keep WorkbookEditor clean");
    }
    check(data.cell_count() == data_cell_count,
        label + " should preserve Data sparse count");
    check(data.estimated_memory_usage() == data_memory,
        label + " should preserve Data memory");
    check(untouched.cell_count() == untouched_cell_count,
        label + " should preserve Untouched sparse count");
    check(untouched.estimated_memory_usage() == untouched_memory,
        label + " should preserve Untouched memory");
}

void check_public_two_clean_single_dirty_materialized_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& dirty,
    fastxlsx::WorksheetEditor& clean,
    std::string_view expected_dirty_name,
    std::size_t expected_dirty_cell_count,
    std::size_t expected_pending_count,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(dirty.has_pending_changes() && !clean.has_pending_changes(),
        label + " should dirty only the expected handle");
    {
        const std::vector<std::string> dirty_names =
            editor.pending_materialized_worksheet_names();
        check(dirty_names == std::vector<std::string>{std::string(expected_dirty_name)},
            label + " should expose only the expected dirty handle");
    }
    check(editor.pending_materialized_cell_count() == expected_dirty_cell_count,
        label + " should expose the expected dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() > 0,
        label + " should expose dirty materialized memory");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should preserve materialized handoff count");
}

void check_public_two_clean_both_dirty_materialized_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& untouched,
    std::size_t expected_dirty_cell_count,
    std::size_t expected_dirty_memory,
    std::size_t expected_pending_count,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(data.has_pending_changes() && untouched.has_pending_changes(),
        label + " should preserve both dirty handles");
    {
        const std::vector<std::string> dirty_names =
            editor.pending_materialized_worksheet_names();
        check(dirty_names == std::vector<std::string>{"Data", "Untouched"},
            label + " should expose both dirty handle names");
    }
    check(editor.pending_materialized_cell_count() == expected_dirty_cell_count,
        label + " should expose the expected dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == expected_dirty_memory,
        label + " should expose the expected dirty memory estimate");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should preserve materialized handoff count");
}

enum class PublicMaterializedGuardDiagnostic {
    RenameSheet,
    ReplaceSheetData,
};

std::string_view public_materialized_guard_fragment(
    PublicMaterializedGuardDiagnostic diagnostic)
{
    switch (diagnostic) {
    case PublicMaterializedGuardDiagnostic::RenameSheet:
        return "cannot rename sheet after materializing planned worksheet session";
    case PublicMaterializedGuardDiagnostic::ReplaceSheetData:
        return "cannot replace sheet data after materializing planned worksheet session";
    }

    return {};
}

std::string_view public_materialized_guard_action_fragment(
    PublicMaterializedGuardDiagnostic diagnostic)
{
    switch (diagnostic) {
    case PublicMaterializedGuardDiagnostic::RenameSheet:
        return "rename sheet";
    case PublicMaterializedGuardDiagnostic::ReplaceSheetData:
        return "replace sheet data";
    }

    return {};
}

std::optional<std::string> check_public_materialized_guard_error(
    const fastxlsx::WorkbookEditor& editor,
    PublicMaterializedGuardDiagnostic expected_guard,
    std::string_view scenario,
    std::optional<PublicMaterializedGuardDiagnostic> stale_guard = std::nullopt)
{
    const std::string label = std::string(scenario);
    const std::optional<std::string> error = editor.last_edit_error();

    check(error.has_value(), label + " should populate last_edit_error");
    if (error.has_value()) {
        check_contains(*error, public_materialized_guard_fragment(expected_guard),
            label + " should report the materialized-session guard");
        if (stale_guard.has_value()) {
            check_not_contains(*error,
                public_materialized_guard_action_fragment(*stale_guard),
                label + " should replace the prior guard diagnostic");
        }
    }

    return error;
}

template <typename Action>
std::optional<std::string> check_public_same_sheet_guard_failure(
    fastxlsx::WorkbookEditor& editor,
    Action&& action,
    PublicMaterializedGuardDiagnostic expected_guard,
    std::string_view scenario,
    std::optional<PublicMaterializedGuardDiagnostic> stale_guard = std::nullopt)
{
    const std::string label = std::string(scenario);

    check(threw_fastxlsx_error(std::forward<Action>(action)),
        label + " should fail");
    return check_public_materialized_guard_error(
        editor, expected_guard, label, stale_guard);
}

void check_public_preserved_sheet_diagnostics(
    fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_cell_count,
    std::size_t expected_memory,
    std::string_view sheet_name,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);
    const std::string name = std::string(sheet_name);

    check(sheet.cell_count() == expected_cell_count,
        label + " should preserve " + name + " sparse count");
    check(sheet.estimated_memory_usage() == expected_memory,
        label + " should preserve " + name + " memory");
}

void check_public_single_sheet_cross_sheet_success_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_cell_count,
    std::size_t expected_memory,
    std::size_t expected_pending_count,
    std::string_view sheet_name,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(!editor.last_edit_error().has_value(),
        label + " should clear last_edit_error");
    check(editor.has_pending_changes(),
        label + " should dirty the editor");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should queue the expected public edit count");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        label + " should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " should keep dirty materialized memory empty");
    check(!sheet.has_pending_changes(),
        label + " should keep the borrowed handle clean");
    check_public_preserved_sheet_diagnostics(
        sheet, expected_cell_count, expected_memory, sheet_name, label);
}

void check_public_single_sheet_mutation_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_cell_count,
    std::size_t expected_pending_count,
    std::string_view expected_dirty_name,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(!editor.last_edit_error().has_value(),
        label + " should clear last_edit_error");
    check(editor.has_pending_changes(),
        label + " should dirty the editor");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should preserve the queued public edit count");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{std::string(expected_dirty_name)},
        label + " should expose the expected dirty materialized sheet");
    check(editor.pending_materialized_cell_count() == expected_cell_count,
        label + " should expose the expected dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() > 0,
        label + " should expose dirty materialized memory");
    check(sheet.has_pending_changes(),
        label + " should dirty the borrowed handle");
    check(sheet.cell_count() == expected_cell_count,
        label + " should expose the expected sparse cell count");
    check(sheet.estimated_memory_usage() > 0,
        label + " should expose dirty sheet memory");
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

void check_public_failed_same_sheet_patch_readonly_clean_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::optional<std::string>& expected_error,
    std::string_view sheet_name,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);
    const std::string name = std::string(sheet_name);

    check(!sheet.has_pending_changes(),
        label + " should keep " + name + " clean");
    check(!editor.has_pending_changes(),
        label + " should keep editor clean");
    check(editor.pending_change_count() == 0,
        label + " should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        label + " should not expose dirty cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " should not expose dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        label + " should not expose summaries");
    check_public_inspection_preserves_last_edit_error(editor, expected_error);
}

void check_public_failed_same_sheet_patch_saved_clean_state(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_pending_count,
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& expected_summaries,
    const std::optional<std::string>& expected_error,
    std::string_view sheet_name,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);
    const std::string name = std::string(sheet_name);

    check(!sheet.has_pending_changes(),
        label + " should keep " + name + " clean");
    check(editor.pending_change_count() == expected_pending_count,
        label + " should preserve saved handoff count");
    check(editor.pending_materialized_worksheet_names().empty(),
        label + " should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        label + " should not expose dirty cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        label + " should not expose dirty memory");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), expected_summaries),
        label + " should preserve summaries");
    check_public_inspection_preserves_last_edit_error(editor, expected_error);
}

const std::string& check_public_two_clean_retry_entry(
    const std::map<std::string, std::string>& entries,
    std::string_view entry_name)
{
    return entries.at(std::string(entry_name));
}

void check_public_two_clean_retry_no_rejected_prefix(
    const std::map<std::string, std::string>& entries,
    std::string_view rejected_prefix,
    std::string_view scenario)
{
    if (rejected_prefix.empty()) {
        return;
    }

    const std::string label = std::string(scenario);
    const std::string combined =
        check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml") +
        check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml");
    check_not_contains(combined, rejected_prefix,
        label + " should not leak rejected mutation payloads");
}

void check_public_two_clean_retry_readonly_first_output(
    const std::map<std::string, std::string>& entries,
    std::string_view data_text,
    std::string_view untouched_text,
    std::string_view rejected_replacement_text,
    std::string_view scenario,
    std::string_view rejected_prefix = {})
{
    const std::string label = std::string(scenario);

    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_text, label + " should include saved Data value");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml"),
        untouched_text, label + " should include saved Untouched value");
    check_not_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        rejected_replacement_text, label + " should not leak rejected replacement");
    check_public_two_clean_retry_no_rejected_prefix(entries, rejected_prefix, label);
}

void check_public_two_clean_retry_readonly_followup_output(
    const std::map<std::string, std::string>& entries,
    std::string_view data_text,
    std::string_view untouched_text,
    std::string_view followup_entry_name,
    std::string_view followup_text,
    std::string_view scenario,
    std::string_view rejected_prefix = {})
{
    const std::string label = std::string(scenario);

    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_text, label + " should keep saved Data value");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml"),
        untouched_text, label + " should keep saved Untouched value");
    check_contains(check_public_two_clean_retry_entry(entries, followup_entry_name),
        followup_text, label + " should include follow-up mutation");
    check_public_two_clean_retry_no_rejected_prefix(entries, rejected_prefix, label);
}

void check_public_two_clean_retry_saved_clean_recovery_output(
    const std::map<std::string, std::string>& entries,
    std::string_view blocked_catalog_name,
    std::string_view data_recovered_text,
    std::string_view untouched_recovered_text,
    std::string_view scenario,
    std::string_view rejected_prefix = {})
{
    const std::string label = std::string(scenario);

    check_contains(check_public_two_clean_retry_entry(entries, "xl/workbook.xml"),
        R"(name="Data")", label + " should preserve Data catalog name");
    check_not_contains(check_public_two_clean_retry_entry(entries, "xl/workbook.xml"),
        blocked_catalog_name, label + " should not leak rejected rename");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_recovered_text, label + " should include Data recovery value");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml"),
        untouched_recovered_text, label + " should include Untouched recovery value");
    check_public_two_clean_retry_no_rejected_prefix(entries, rejected_prefix, label);
}

void check_public_two_clean_retry_saved_clean_followup_output(
    const std::map<std::string, std::string>& entries,
    std::string_view data_first_text,
    std::string_view data_recovered_text,
    std::string_view untouched_recovered_text,
    std::string_view followup_entry_name,
    std::string_view followup_text,
    std::string_view scenario,
    std::string_view rejected_prefix = {})
{
    const std::string label = std::string(scenario);

    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_first_text, label + " should keep Data first value");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_recovered_text, label + " should keep Data recovery value");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml"),
        untouched_recovered_text, label + " should keep Untouched recovery value");
    check_contains(check_public_two_clean_retry_entry(entries, followup_entry_name),
        followup_text, label + " should include follow-up mutation");
    check_public_two_clean_retry_no_rejected_prefix(entries, rejected_prefix, label);
}

void check_public_two_clean_recovery_copy_original_output(
    const std::map<std::string, std::string>& entries,
    const std::map<std::string, std::string>& source_entries,
    std::string_view data_rejected_text,
    std::string_view untouched_rejected_text,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(entries == source_entries, label + " should remain copy-original");
    check_not_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_rejected_text, label + " should not leak rejected Data payload");
    check_not_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml"),
        untouched_rejected_text, label + " should not leak rejected Untouched payload");
}

void check_public_two_clean_saved_clean_output(
    const std::map<std::string, std::string>& entries,
    std::string_view blocked_catalog_name,
    std::string_view data_text,
    std::string_view untouched_text,
    std::string_view scenario,
    std::string_view untouched_rejected_text = {})
{
    const std::string label = std::string(scenario);

    check_contains(check_public_two_clean_retry_entry(entries, "xl/workbook.xml"),
        R"(name="Data")", label + " should preserve Data catalog name");
    check_not_contains(check_public_two_clean_retry_entry(entries, "xl/workbook.xml"),
        blocked_catalog_name, label + " should not leak rejected Data rename");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_text, label + " should persist Data value");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml"),
        untouched_text, label + " should persist Untouched value");
    if (!untouched_rejected_text.empty()) {
        check_not_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml"),
            untouched_rejected_text, label + " should not leak rejected Untouched payload");
    }
}

void check_public_two_clean_other_mutation_readonly_output(
    const std::map<std::string, std::string>& entries,
    const std::map<std::string, std::string>& source_entries,
    std::string_view data_rejected_text,
    std::string_view untouched_mutated_text,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml") ==
            check_public_two_clean_retry_entry(source_entries, "xl/worksheets/sheet1.xml"),
        label + " should preserve Data bytes");
    check_not_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_rejected_text, label + " should not leak rejected Data payload");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml"),
        untouched_mutated_text, label + " should persist Untouched mutation");
}

void check_public_two_clean_failed_save_readonly_output(
    const std::map<std::string, std::string>& entries,
    std::string_view data_text,
    std::string_view untouched_text,
    std::string_view data_rejected_text,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_text, label + " should persist Data mutation");
    check_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet2.xml"),
        untouched_text, label + " should persist Untouched mutation");
    check_not_contains(check_public_two_clean_retry_entry(entries, "xl/worksheets/sheet1.xml"),
        data_rejected_text, label + " should not leak rejected Data payload");
}

std::filesystem::path artifact(std::string_view name)
{
    return fastxlsx::test::artifact_path(name);
}

std::filesystem::path repository_root()
{
    static const std::filesystem::path root = [] {
        std::filesystem::path current = std::filesystem::path(__FILE__).parent_path();
        if (current.is_relative()) {
            current = std::filesystem::absolute(current);
        }

        while (true) {
            const std::filesystem::path marker =
                current / "docs" / "assets" / "donation" / "weixin.png";
            if (std::filesystem::exists(marker)) {
                return current;
            }

            const std::filesystem::path parent = current.parent_path();
            if (parent.empty() || parent == current) {
                break;
            }
            current = parent;
        }

        throw std::runtime_error("failed to locate repository root for workbook editor tests");
    }();

    return root;
}

std::filesystem::path repository_asset(std::string_view relative_path)
{
    return repository_root() / std::filesystem::path(relative_path);
}

std::span<const std::byte> as_bytes(std::string_view text)
{
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

void check_public_worksheet_materialization_failure_hygiene(
    const std::filesystem::path& source,
    const std::filesystem::path& output,
    std::string_view expected_diagnostic,
    std::string_view replacement_text,
    std::string_view scenario,
    std::string_view recovery_sheet_name = "Data",
    std::string_view output_entry_name = "xl/worksheets/sheet1.xml",
    std::string_view target_sheet_name = "Data")
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.has_worksheet("Data"),
        std::string(scenario) + " should preserve planned sheet catalog");
    check(editor.has_source_worksheet("Data"),
        std::string(scenario) + " should preserve source sheet catalog");
    check(editor.has_worksheet(std::string(target_sheet_name)),
        std::string(scenario) + " should preserve target planned sheet catalog");
    check(editor.has_source_worksheet(std::string(target_sheet_name)),
        std::string(scenario) + " should preserve target source sheet catalog");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();

    bool try_failed = false;
    try {
        (void)editor.try_worksheet(std::string(target_sheet_name));
    } catch (const fastxlsx::FastXlsxError& error) {
        try_failed = true;
        check_contains(error.what(), expected_diagnostic,
            std::string(scenario)
                + " try_worksheet should expose the materialization diagnostic");
    }
    check(try_failed,
        std::string(scenario) + " try_worksheet should fail for non-missing sheets");
    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        scenario,
        "try_worksheet failure",
        recovery_sheet_name);
    check(!editor.has_pending_replacement(std::string(target_sheet_name)),
        std::string(scenario) + " try_worksheet failure should not report a target replacement");

    bool worksheet_failed = false;
    try {
        (void)editor.worksheet(std::string(target_sheet_name));
    } catch (const fastxlsx::FastXlsxError& error) {
        worksheet_failed = true;
        check_contains(error.what(), expected_diagnostic,
            std::string(scenario)
                + " worksheet should expose the materialization diagnostic");
    }
    check(worksheet_failed,
        std::string(scenario) + " worksheet should reject invalid source materialization");
    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        scenario,
        "worksheet failure",
        recovery_sheet_name);
    check(!editor.has_pending_replacement(std::string(target_sheet_name)),
        std::string(scenario) + " worksheet failure should not report a target replacement");

    editor.replace_sheet_data(std::string(recovery_sheet_name),
        {{fastxlsx::CellValue::text(std::string(replacement_text))}});
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at(std::string(output_entry_name)), replacement_text,
        std::string(scenario) + " editor should remain usable after materialization failure");
}

void write_binary_file(const std::filesystem::path& path, std::string_view data)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("failed to open test artifact for writing");
    }
    if (!data.empty()) {
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    if (!stream) {
        throw std::runtime_error("failed to write test artifact");
    }
}

std::size_t find_end_of_central_directory(const std::string& data)
{
    if (data.size() < 22) {
        throw std::runtime_error("test ZIP package is too small");
    }

    for (std::size_t offset = data.size() - 22; offset != static_cast<std::size_t>(-1);
         --offset) {
        if (fastxlsx::test::read_u32(data, offset) == 0x06054b50u) {
            return offset;
        }
        if (offset == 0) {
            break;
        }
    }

    throw std::runtime_error("test ZIP end of central directory not found");
}

struct ZipEntryPayloadLocation {
    std::size_t central_offset = 0;
    std::size_t data_offset = 0;
    std::uint32_t compressed_size = 0;
};

ZipEntryPayloadLocation find_zip_entry_payload_location(
    const std::string& data, std::string_view name)
{
    const std::size_t eocd_offset = find_end_of_central_directory(data);
    const std::uint16_t entry_count =
        fastxlsx::test::read_u16(data, eocd_offset + 10u);
    std::size_t offset = fastxlsx::test::read_u32(data, eocd_offset + 16u);

    for (std::uint16_t index = 0; index < entry_count; ++index) {
        if (offset + 46u > data.size()
            || fastxlsx::test::read_u32(data, offset) != 0x02014b50u) {
            throw std::runtime_error("test ZIP central directory entry is invalid");
        }

        const std::uint16_t name_size = fastxlsx::test::read_u16(data, offset + 28u);
        const std::uint16_t extra_size = fastxlsx::test::read_u16(data, offset + 30u);
        const std::uint16_t comment_size = fastxlsx::test::read_u16(data, offset + 32u);
        const std::size_t record_size = 46u + name_size + extra_size + comment_size;
        if (offset + record_size > data.size()) {
            throw std::runtime_error("test ZIP central directory entry is truncated");
        }

        const std::string entry_name = data.substr(offset + 46u, name_size);
        if (entry_name == name) {
            const std::uint32_t compressed_size =
                fastxlsx::test::read_u32(data, offset + 20u);
            const std::size_t local_offset =
                fastxlsx::test::read_u32(data, offset + 42u);
            if (local_offset + 30u > data.size()
                || fastxlsx::test::read_u32(data, local_offset) != 0x04034b50u) {
                throw std::runtime_error("test ZIP local header entry is invalid");
            }

            const std::uint16_t local_name_size =
                fastxlsx::test::read_u16(data, local_offset + 26u);
            const std::uint16_t local_extra_size =
                fastxlsx::test::read_u16(data, local_offset + 28u);
            const std::size_t data_offset =
                local_offset + 30u + local_name_size + local_extra_size;
            if (compressed_size == 0 || data_offset + compressed_size > data.size()) {
                throw std::runtime_error("test ZIP entry payload is invalid");
            }
            return {offset, data_offset, compressed_size};
        }

        offset += record_size;
    }

    throw std::runtime_error("test ZIP entry not found");
}

void write_u32(std::string& data, std::size_t offset, std::uint32_t value)
{
    if (offset + 4u > data.size()) {
        throw std::runtime_error("test ZIP patch offset is out of range");
    }
    data[offset] = static_cast<char>(value & 0xffu);
    data[offset + 1u] = static_cast<char>((value >> 8u) & 0xffu);
    data[offset + 2u] = static_cast<char>((value >> 16u) & 0xffu);
    data[offset + 3u] = static_cast<char>((value >> 24u) & 0xffu);
}

void append_u16(std::string& output, std::uint16_t value)
{
    output.push_back(static_cast<char>(value & 0xffu));
    output.push_back(static_cast<char>((value >> 8u) & 0xffu));
}

void append_u32(std::string& output, std::uint32_t value)
{
    output.push_back(static_cast<char>(value & 0xffu));
    output.push_back(static_cast<char>((value >> 8u) & 0xffu));
    output.push_back(static_cast<char>((value >> 16u) & 0xffu));
    output.push_back(static_cast<char>((value >> 24u) & 0xffu));
}

std::uint16_t checked_zip_u16(std::size_t value, std::string_view field)
{
    if (value > 0xffffu) {
        throw std::runtime_error(std::string("test ZIP field exceeds uint16: ")
            + std::string(field));
    }
    return static_cast<std::uint16_t>(value);
}

std::uint32_t checked_zip_u32(std::size_t value, std::string_view field)
{
    if (value > 0xffffffffu) {
        throw std::runtime_error(std::string("test ZIP field exceeds uint32: ")
            + std::string(field));
    }
    return static_cast<std::uint32_t>(value);
}

const std::array<std::uint32_t, 256>& test_crc32_table()
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        constexpr std::uint32_t polynomial = 0xedb88320u;
        for (std::uint32_t i = 0; i < values.size(); ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 1u) != 0u ? (crc >> 1u) ^ polynomial : crc >> 1u;
            }
            values[i] = crc;
        }
        return values;
    }();
    return table;
}

std::uint32_t test_crc32(std::string_view data)
{
    std::uint32_t crc = 0xffffffffu;
    const auto& table = test_crc32_table();
    for (unsigned char byte : data) {
        crc = (crc >> 8u) ^ table[(crc ^ byte) & 0xffu];
    }
    return crc ^ 0xffffffffu;
}

void write_stored_zip_entries(
    const std::filesystem::path& path, const std::map<std::string, std::string>& entries)
{
    struct CentralRecord {
        std::string name;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t local_header_offset = 0;
    };

    std::string archive;
    std::vector<CentralRecord> central_records;
    central_records.reserve(entries.size());

    for (const auto& [name, payload] : entries) {
        const std::uint16_t name_size = checked_zip_u16(name.size(), "entry name");
        const std::uint32_t payload_size = checked_zip_u32(payload.size(), "entry payload");
        const std::uint32_t local_header_offset =
            checked_zip_u32(archive.size(), "local header offset");
        const std::uint32_t crc = test_crc32(payload);

        append_u32(archive, 0x04034b50u);
        append_u16(archive, 20);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u32(archive, crc);
        append_u32(archive, payload_size);
        append_u32(archive, payload_size);
        append_u16(archive, name_size);
        append_u16(archive, 0);
        archive.append(name);
        archive.append(payload);

        central_records.push_back({name, crc, payload_size, local_header_offset});
    }

    const std::uint32_t central_directory_offset =
        checked_zip_u32(archive.size(), "central directory offset");
    for (const CentralRecord& record : central_records) {
        const std::uint16_t name_size = checked_zip_u16(record.name.size(), "entry name");
        append_u32(archive, 0x02014b50u);
        append_u16(archive, 20);
        append_u16(archive, 20);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u32(archive, record.crc);
        append_u32(archive, record.size);
        append_u32(archive, record.size);
        append_u16(archive, name_size);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u32(archive, 0);
        append_u32(archive, record.local_header_offset);
        archive.append(record.name);
    }

    const std::uint32_t central_directory_size =
        checked_zip_u32(archive.size() - central_directory_offset, "central directory size");
    const std::uint16_t entry_count = checked_zip_u16(entries.size(), "entry count");
    append_u32(archive, 0x06054b50u);
    append_u16(archive, 0);
    append_u16(archive, 0);
    append_u16(archive, entry_count);
    append_u16(archive, entry_count);
    append_u32(archive, central_directory_size);
    append_u32(archive, central_directory_offset);
    append_u16(archive, 0);

    write_binary_file(path, archive);
}

void rewrite_package_entry_as_stored(
    const std::filesystem::path& path, std::string_view entry_name, std::string replacement)
{
    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(path);
    auto entry = entries.find(std::string(entry_name));
    if (entry == entries.end()) {
        throw std::runtime_error("test package entry to rewrite was not found");
    }
    entry->second = std::move(replacement);
    write_stored_zip_entries(path, entries);
}

void replace_first_or_throw(
    std::string& value, std::string_view needle, std::string_view replacement)
{
    const std::size_t position = value.find(needle);
    if (position == std::string::npos) {
        throw std::runtime_error("test string replacement target was not found");
    }
    value.replace(position, needle.size(), replacement.data(), replacement.size());
}

void corrupt_zip_entry_payload(std::string& data, std::string_view entry_name)
{
    const ZipEntryPayloadLocation location =
        find_zip_entry_payload_location(data, entry_name);
    const std::size_t corrupt_offset =
        location.data_offset + static_cast<std::size_t>(location.compressed_size / 2u);
    data[corrupt_offset] = data[corrupt_offset] == 'X' ? 'Y' : 'X';
}

void corrupt_zip_entry_crc_metadata(std::string& data, std::string_view entry_name)
{
    const ZipEntryPayloadLocation location =
        find_zip_entry_payload_location(data, entry_name);
    const std::size_t central_crc_offset = location.central_offset + 16u;
    const std::uint32_t crc = fastxlsx::test::read_u32(data, central_crc_offset);
    write_u32(data, central_crc_offset, crc ^ 0xffffffffu);
}

// Writes a small two-sheet workbook through the public streaming writer. The
// first sheet carries placeholder rows to be replaced; the second sheet is left
// untouched so preservation can be checked.
std::filesystem::path write_two_sheet_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_stationary_formula(
    std::string_view name, std::string_view formula)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0),
            fastxlsx::CellView::formula(formula)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_delete_row_ref_formula(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("row1-a"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("row2-a")});
        data.append_row({fastxlsx::CellView::text("row3-a")});
        data.append_row({fastxlsx::CellView::text("row4-a"),
            fastxlsx::CellView::text("row4-b"),
            fastxlsx::CellView::formula("Data!A1+Data!A:A+Data!1:1+Data!B4")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_delete_column_ref_formula(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("row1-a"),
            fastxlsx::CellView::number(1.0),
            fastxlsx::CellView::text("row1-c"),
            fastxlsx::CellView::formula("Data!A1+Data!A:A+Data!1:1+Data!D2")});
        data.append_row({fastxlsx::CellView::text("row2-a"),
            fastxlsx::CellView::text("row2-b"),
            fastxlsx::CellView::text("row2-c"),
            fastxlsx::CellView::text("row2-d")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

template <typename ShiftOperation>
void check_public_stationary_formula_shift_case(
    std::string_view source_name,
    std::string_view output_name,
    std::string_view noop_output_name,
    std::string_view initial_formula,
    std::string_view expected_formula,
    std::string_view label,
    ShiftOperation shift_operation)
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(source_name, initial_formula);
    const std::filesystem::path output = artifact(output_name);
    const std::filesystem::path noop_output = artifact(noop_output_name);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(!sheet.has_pending_changes(),
        std::string(label) + " setup should start with a clean materialized sheet");
    check(editor.pending_change_count() == 0,
        std::string(label) + " setup should not queue Patch edits");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        std::string(label) + " setup should keep dirty materialized diagnostics empty");

    shift_operation(sheet);

    check(sheet.has_pending_changes(),
        std::string(label) + " should dirty the sheet when only formula references change");
    check(sheet.cell_count() == 4,
        std::string(label) + " should keep sparse cell count stable");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        std::string(label) + " should keep sparse bounds stable");
    const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
    check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
            rewritten_formula.text_value() == expected_formula,
        std::string(label) + " should rewrite the stationary source formula");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
            sheet.get_cell("B1").number_value() == 1.0 &&
            sheet.get_cell("A2").text_value() == "placeholder-a2",
        std::string(label) + " should leave represented sparse coordinates in place");
    const auto check_stationary_formula_snapshots =
        [&](fastxlsx::WorksheetEditor& snapshot_sheet, std::string_view scenario_label) {
            const std::string scenario(scenario_label);
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                snapshot_sheet.row_cells(1);
            check(row_one.size() == 3 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "placeholder-a1" &&
                    row_one[1].reference.row == 1 &&
                    row_one[1].reference.column == 2 &&
                    row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    row_one[1].value.number_value() == 1.0 &&
                    row_one[2].reference.row == 1 &&
                    row_one[2].reference.column == 3 &&
                    row_one[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    row_one[2].value.text_value() == expected_formula,
                scenario + " row_cells should expose the stationary rewritten formula row");
            const std::vector<fastxlsx::WorksheetCellSnapshot> formula_column =
                snapshot_sheet.column_cells(3);
            check(formula_column.size() == 1 &&
                    formula_column[0].reference.row == 1 &&
                    formula_column[0].reference.column == 3 &&
                    formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    formula_column[0].value.text_value() == expected_formula,
                scenario + " column_cells should expose the stationary rewritten formula");
        };
    check_stationary_formula_snapshots(
        sheet, std::string(label) + " live before save");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_change_count() == 0,
        std::string(label) + " should not flush materialized state before save_as");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        std::string(label) + " should report Data dirty");
    check(editor.pending_materialized_cell_count() == 4,
        std::string(label) + " should report stable dirty sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        std::string(label) + " should report dirty sparse memory");
    check(!editor.last_edit_error().has_value(),
        std::string(label) + " should keep diagnostics clear");

    const auto source_entries_before_failed_save = fastxlsx::test::read_zip_entries(source);
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        std::string(label) + " failed save should reject exact source overwrite");
    check(sheet.has_pending_changes(),
        std::string(label) + " failed save should preserve dirty formula-only state");
    check(editor.pending_change_count() == 0,
        std::string(label) + " failed save should not queue a materialized handoff");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        std::string(label) + " failed save should preserve dirty materialized names");
    check(editor.pending_materialized_cell_count() == 4,
        std::string(label) + " failed save should preserve dirty sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        std::string(label) + " failed save should preserve dirty sparse memory");
    const fastxlsx::CellValue formula_after_failed_save = sheet.get_cell("C1");
    check(formula_after_failed_save.kind() == fastxlsx::CellValueKind::Formula &&
            formula_after_failed_save.text_value() == expected_formula,
        std::string(label) + " failed save should preserve the rewritten formula");
    check_stationary_formula_snapshots(
        sheet, std::string(label) + " failed save live state");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        std::string(label) + " failed save should leave source package bytes unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        std::string(label) + " save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        std::string(label) + " save_as should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        std::string(label) + " save_as should clear dirty materialized diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        std::string(label) + " save_as should clear dirty edit summaries");
    check_stationary_formula_snapshots(
        sheet, std::string(label) + " saved live handle");
    check(!sheet.has_pending_changes(),
        std::string(label) + " saved live snapshots should keep the handle clean");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string formula_xml =
        std::string(R"(<c r="C1"><f>)") + std::string(expected_formula) + R"(</f></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        std::string(label) + " save_as should keep sparse dimension stable");
    check_contains(worksheet_xml, formula_xml,
        std::string(label) + " save_as should write the rewritten formula");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        std::string(label) + " save_as should preserve untouched worksheets");

    const auto check_reopened_output =
        [&](const std::filesystem::path& path, std::string_view scenario_label) {
            const std::string scenario(scenario_label);
            fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(path);
            fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
            check(!reopened_sheet.has_pending_changes(),
                scenario + " should materialize cleanly");
            check(reopened_sheet.cell_count() == 4,
                scenario + " should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                scenario + " should keep sparse bounds");
            const fastxlsx::CellValue reopened_formula = reopened_sheet.get_cell("C1");
            check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_formula.text_value() == expected_formula,
                scenario + " should read the rewritten formula");
            check_stationary_formula_snapshots(reopened_sheet, scenario);
            check(reopened.pending_materialized_worksheet_names().empty() &&
                    reopened.pending_materialized_cell_count() == 0 &&
                    reopened.estimated_pending_materialized_memory_usage() == 0,
                scenario + " should keep dirty diagnostics empty");
            check(reopened.pending_worksheet_edits().empty() &&
                    !reopened.last_edit_error().has_value(),
                scenario + " should keep public state clean");
        };

    check_reopened_output(output, std::string(label) + " reopened output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes(),
        std::string(label) + " no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        std::string(label) + " no-op save should not add a second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        std::string(label) + " no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        std::string(label) + " no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, std::string(label) + " no-op save");
    check(!editor.last_edit_error().has_value(),
        std::string(label) + " no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, std::string(label) + " no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, std::string(label) + " no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        std::string(label) + " no-op output should match the materialized output");
    check_reopened_output(noop_output, std::string(label) + " no-op output");
}

void check_stationary_formula_saved_reopen_snapshots(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view expected_formula,
    std::string_view label)
{
    const std::string scenario(label);
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 3 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == "placeholder-a1" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0 &&
            row_one[2].reference.row == 1 &&
            row_one[2].reference.column == 3 &&
            row_one[2].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_one[2].value.text_value() == expected_formula,
        scenario + " row_cells should expose the saved stationary formula row");
    const std::vector<fastxlsx::WorksheetCellSnapshot> formula_column =
        sheet.column_cells(3);
    check(formula_column.size() == 1 &&
            formula_column[0].reference.row == 1 &&
            formula_column[0].reference.column == 3 &&
            formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            formula_column[0].value.text_value() == expected_formula,
        scenario + " column_cells should expose the saved stationary formula");
}

void check_materialized_only_formula_row_column_snapshots(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view expected_formula,
    std::string_view label)
{
    const std::string scenario(label);
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == "placeholder-a1" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        scenario + " row_cells should expose source row one");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two = sheet.row_cells(2);
    check(row_two.size() == 2 &&
            row_two[0].reference.row == 2 &&
            row_two[0].reference.column == 1 &&
            row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_two[0].value.text_value() == "placeholder-a2" &&
            row_two[1].reference.row == 2 &&
            row_two[1].reference.column == 3 &&
            row_two[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_two[1].value.text_value() == expected_formula,
        scenario + " row_cells should expose the saved materialized-only formula row");
    const std::vector<fastxlsx::WorksheetCellSnapshot> source_column =
        sheet.column_cells(1);
    check(source_column.size() == 2 &&
            source_column[0].reference.row == 1 &&
            source_column[0].reference.column == 1 &&
            source_column[0].value.kind() == fastxlsx::CellValueKind::Text &&
            source_column[0].value.text_value() == "placeholder-a1" &&
            source_column[1].reference.row == 2 &&
            source_column[1].reference.column == 1 &&
            source_column[1].value.kind() == fastxlsx::CellValueKind::Text &&
            source_column[1].value.text_value() == "placeholder-a2",
        scenario + " column_cells should expose source text cells");
    const std::vector<fastxlsx::WorksheetCellSnapshot> formula_column =
        sheet.column_cells(3);
    check(formula_column.size() == 1 &&
            formula_column[0].reference.row == 2 &&
            formula_column[0].reference.column == 3 &&
            formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            formula_column[0].value.text_value() == expected_formula,
        scenario + " column_cells should expose the saved materialized-only formula");
}

void check_materialized_only_formula_saved_reopen_snapshots(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view expected_formula,
    std::string_view label)
{
    check_materialized_only_formula_row_column_snapshots(
        sheet, expected_formula, label);
}

void check_delete_row_ref_formula_saved_reopen_snapshots(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view expected_formula,
    std::string_view label)
{
    const std::string scenario(label);
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 1 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == "placeholder-a2",
        scenario + " row_cells should expose the shifted source row");
    check(sheet.row_cells(2).empty(),
        scenario + " row_cells should keep the deleted-row gap empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> formula_row = sheet.row_cells(3);
    check(formula_row.size() == 1 &&
            formula_row[0].reference.row == 3 &&
            formula_row[0].reference.column == 3 &&
            formula_row[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            formula_row[0].value.text_value() == expected_formula,
        scenario + " row_cells should expose the shifted #REF! formula row");
    const std::vector<fastxlsx::WorksheetCellSnapshot> source_column =
        sheet.column_cells(1);
    check(source_column.size() == 1 &&
            source_column[0].reference.row == 1 &&
            source_column[0].reference.column == 1 &&
            source_column[0].value.kind() == fastxlsx::CellValueKind::Text &&
            source_column[0].value.text_value() == "placeholder-a2",
        scenario + " column_cells should expose the shifted source cell");
    check(sheet.column_cells(2).empty(),
        scenario + " column_cells should keep the empty middle column absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> formula_column =
        sheet.column_cells(3);
    check(formula_column.size() == 1 &&
            formula_column[0].reference.row == 3 &&
            formula_column[0].reference.column == 3 &&
            formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            formula_column[0].value.text_value() == expected_formula,
        scenario + " column_cells should expose the shifted #REF! formula");
}

void check_delete_column_ref_formula_saved_reopen_snapshots(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view expected_formula,
    std::string_view label)
{
    const std::string scenario(label);
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[0].value.number_value() == 1.0 &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 3 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_one[1].value.text_value() == expected_formula,
        scenario + " row_cells should expose shifted source and #REF! formula cells");
    check(sheet.row_cells(2).empty(),
        scenario + " row_cells should keep the deleted-column row empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> source_column =
        sheet.column_cells(1);
    check(source_column.size() == 1 &&
            source_column[0].reference.row == 1 &&
            source_column[0].reference.column == 1 &&
            source_column[0].value.kind() == fastxlsx::CellValueKind::Number &&
            source_column[0].value.number_value() == 1.0,
        scenario + " column_cells should expose the shifted source number");
    check(sheet.column_cells(2).empty(),
        scenario + " column_cells should keep the deleted-column gap empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> formula_column =
        sheet.column_cells(3);
    check(formula_column.size() == 1 &&
            formula_column[0].reference.row == 1 &&
            formula_column[0].reference.column == 3 &&
            formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            formula_column[0].value.text_value() == expected_formula,
        scenario + " column_cells should expose the shifted #REF! formula");
}

std::filesystem::path write_two_sheet_source_with_large_clear_payload(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    const std::string large_a1 = "large-clear-a1-" + std::string(4096, 'a');
    const std::string large_a2 = "large-clear-a2-" + std::string(4096, 'b');

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text(large_a1),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text(large_a2),
            fastxlsx::CellView::text("clear-column-tail-b2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me")});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_large_clear_range_payload(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    const std::string large_a1 = "large-clear-range-a1-" + std::string(4096, 'a');
    const std::string large_a2 = "large-clear-range-a2-" + std::string(4096, 'b');

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text(large_a1),
            fastxlsx::CellView::number(1.0),
            fastxlsx::CellView::text("clear-range-c1")});
        data.append_row({fastxlsx::CellView::text(large_a2),
            fastxlsx::CellView::text("clear-range-b2"),
            fastxlsx::CellView::text("clear-range-c2")});
        data.append_row({fastxlsx::CellView::text("clear-range-a3"),
            fastxlsx::CellView::text("clear-range-b3"),
            fastxlsx::CellView::text("clear-range-c3")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me")});
    }
    writer.close();

    return path;
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

std::filesystem::path write_two_sheet_source_with_shift_memory_formula(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("anchor-a1")});
        data.append_row({fastxlsx::CellView::formula("A9+A9+A9+A9+A9")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me")});
    }
    writer.close();

    return path;
}

// Writes a source workbook with document properties so patch tests can verify
// that WorkbookEditor preserves docProps bytes through save_as().
std::filesystem::path write_two_sheet_source_with_document_properties(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriterOptions options;
    options.document_properties.creator = "WorkbookEditor Tests";
    options.document_properties.last_modified_by = "WorkbookEditor Tests";
    options.document_properties.title = "WorkbookEditor preservation";
    options.document_properties.subject = "FastXLSX";
    options.document_properties.description = "WorkbookEditor docProps preservation source";
    options.document_properties.keywords = "FastXLSX,WorkbookEditor,Patch";
    options.document_properties.category = "tests";
    options.document_properties.application = "FastXLSX";
    options.document_properties.app_version = "1.0";

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_image(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter pictures = writer.add_worksheet("Pictures");
        pictures.append_row({fastxlsx::CellView::text("image-sheet")});
        pictures.add_image(fastxlsx::test::tiny_png_bytes(), {1, 1, 2, 2});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_two_images(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter pictures = writer.add_worksheet("Pictures");
        pictures.append_row({fastxlsx::CellView::text("image-sheet")});
        pictures.add_image(fastxlsx::test::tiny_png_bytes(), {1, 1, 2, 2});
        pictures.add_image(fastxlsx::test::tiny_jpeg_bytes(), {3, 1, 4, 2});
    }
    writer.close();

    return path;
}

std::filesystem::path write_public_editing_e2e_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriterOptions options;
    options.document_properties.creator = "WorkbookEditor E2E";
    options.document_properties.last_modified_by = "WorkbookEditor E2E";
    options.document_properties.title = "Public editing E2E";
    options.document_properties.subject = "FastXLSX";
    options.document_properties.description = "WorkbookEditor public editing smoke source";
    options.document_properties.keywords = "FastXLSX,WorkbookEditor,editing";
    options.document_properties.category = "tests";
    options.document_properties.application = "FastXLSX";
    options.document_properties.app_version = "1.0";

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter replace_me = writer.add_worksheet("ReplaceMe");
        replace_me.append_row({fastxlsx::CellView::text("replace-old"),
            fastxlsx::CellView::number(5.0)});
    }
    {
        fastxlsx::WorksheetWriter pictures = writer.add_worksheet("Pictures");
        pictures.append_row({fastxlsx::CellView::text("image-sheet")});
        pictures.add_image(fastxlsx::test::tiny_png_bytes(), {1, 1, 2, 2});
    }
    writer.close();

    return path;
}

void check_reopened_clean_sheet_output(
    const std::filesystem::path& output,
    std::string_view sheet_name,
    std::string_view scenario,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet(sheet_name);
    const std::string prefix(scenario);

    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened output should not expose stale diagnostics");
    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened output should not expose dirty diagnostics");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened output should not expose dirty worksheet names");

    inspect(reopened_sheet);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened readback should keep public state clean");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened readback should keep dirty diagnostics empty");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened readback should keep dirty worksheet names empty");
    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened readback should keep last_edit_error empty");
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

void check_reopened_renamed_shift_noop_output(
    const std::filesystem::path& output,
    std::string_view scenario)
{
    check_reopened_clean_sheet_output(
        output, "RenamedData", scenario,
        [](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 3,
                "renamed shift no-op output should keep sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 3,
                "renamed shift no-op output should expose combined bounds");
            check(noop_sheet.get_cell("C1").number_value() == 1.0,
                "renamed shift no-op output should read shifted B1");
            check(noop_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "renamed shift no-op output should read shifted A2");
            check(!noop_sheet.try_cell("B1").has_value() &&
                    !noop_sheet.try_cell("A2").has_value(),
                "renamed shift no-op output should keep old coordinates absent");
        });
}

void check_reopened_default_data_sheet_output(
    const std::filesystem::path& output,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check_reopened_clean_sheet_output(output, "Data", scenario,
        [prefix](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                prefix + " reopened output should keep default source sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                prefix + " reopened output should keep default source used range");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a1",
                prefix + " reopened output should read source-backed A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                prefix + " reopened output should read source-backed B1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "placeholder-a2",
                prefix + " reopened output should read source-backed A2");
        });
}

void check_reopened_default_data_overwrite_output(
    const std::filesystem::path& output,
    std::string_view scenario,
    std::string_view expected_a1_text)
{
    const std::string prefix(scenario);
    const std::string expected_a1(expected_a1_text);
    check_reopened_clean_sheet_output(output, "Data", scenario,
        [prefix, expected_a1](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                prefix + " reopened output should keep source sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                prefix + " reopened output should keep source used range");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == expected_a1,
                prefix + " reopened output should read overwritten A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                prefix + " reopened output should keep source-backed B1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "placeholder-a2",
                prefix + " reopened output should keep source-backed A2");
            check(!reopened_sheet.try_cell("D4").has_value(),
                prefix + " reopened output should keep rejected D4 absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_cells =
                reopened_sheet.sparse_cells();
            check(reopened_cells.size() == 3,
                prefix + " reopened sparse_cells should expose all represented cells");
            if (reopened_cells.size() == 3) {
                check(reopened_cells[0].reference.row == 1 &&
                        reopened_cells[0].reference.column == 1 &&
                        reopened_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_cells[0].value.text_value() == expected_a1,
                    prefix + " reopened sparse_cells should expose overwritten A1 first");
                check(reopened_cells[1].reference.row == 1 &&
                        reopened_cells[1].reference.column == 2 &&
                        reopened_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_cells[1].value.number_value() == 1.0,
                    prefix + " reopened sparse_cells should expose source B1 second");
                check(reopened_cells[2].reference.row == 2 &&
                        reopened_cells[2].reference.column == 1 &&
                        reopened_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_cells[2].value.text_value() == "placeholder-a2",
                    prefix + " reopened sparse_cells should expose source A2 last");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_range_cells =
                reopened_sheet.sparse_cells("A1:D4");
            check(reopened_range_cells.size() == 3,
                prefix + " reopened range sparse_cells should expose all represented cells");
            if (reopened_range_cells.size() == 3) {
                check(reopened_range_cells[0].reference.row == 1 &&
                        reopened_range_cells[0].reference.column == 1 &&
                        reopened_range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_range_cells[0].value.text_value() == expected_a1,
                    prefix + " reopened range sparse_cells should expose overwritten A1 first");
                check(reopened_range_cells[1].reference.row == 1 &&
                        reopened_range_cells[1].reference.column == 2 &&
                        reopened_range_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_range_cells[1].value.number_value() == 1.0,
                    prefix + " reopened range sparse_cells should expose source B1 second");
                check(reopened_range_cells[2].reference.row == 2 &&
                        reopened_range_cells[2].reference.column == 1 &&
                        reopened_range_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_range_cells[2].value.text_value() == "placeholder-a2",
                    prefix + " reopened range sparse_cells should expose source A2 last");
            }
            const std::array<fastxlsx::WorksheetCellReference, 6> reopened_requested_refs {
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {4, 4},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_requested_cells =
                reopened_sheet.sparse_cells(reopened_requested_refs);
            check(reopened_requested_cells.size() == 4,
                prefix + " reopened requested sparse_cells should skip rejected/gap coordinates and keep duplicates");
            if (reopened_requested_cells.size() == 4) {
                check(reopened_requested_cells[0].reference.row == 2 &&
                        reopened_requested_cells[0].reference.column == 1 &&
                        reopened_requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_requested_cells[0].value.text_value() == "placeholder-a2",
                    prefix + " reopened requested sparse_cells should keep A2 first");
                check(reopened_requested_cells[1].reference.row == 1 &&
                        reopened_requested_cells[1].reference.column == 2 &&
                        reopened_requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_requested_cells[1].value.number_value() == 1.0,
                    prefix + " reopened requested sparse_cells should keep B1 after skipped D4");
                check(reopened_requested_cells[2].reference.row == 1 &&
                        reopened_requested_cells[2].reference.column == 1 &&
                        reopened_requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_requested_cells[2].value.text_value() == expected_a1,
                    prefix + " reopened requested sparse_cells should keep overwritten A1 in requested order");
                check(reopened_requested_cells[3].reference.row == 2 &&
                        reopened_requested_cells[3].reference.column == 1 &&
                        reopened_requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_requested_cells[3].value.text_value() == "placeholder-a2",
                    prefix + " reopened requested sparse_cells should preserve duplicate A2");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                reopened_sheet.row_cells(1);
            check(reopened_row_one.size() == 2 &&
                    reopened_row_one[0].reference.row == 1 &&
                    reopened_row_one[0].reference.column == 1 &&
                    reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[0].value.text_value() == expected_a1 &&
                    reopened_row_one[1].reference.row == 1 &&
                    reopened_row_one[1].reference.column == 2 &&
                    reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_row_one[1].value.number_value() == 1.0,
                prefix + " reopened row_cells should expose overwritten A1 and source B1");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 1 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "placeholder-a2",
                prefix + " reopened row_cells should expose source A2");
            check(reopened_sheet.row_cells(3).empty(),
                prefix + " reopened row_cells should keep the gap row empty");
            check(reopened_sheet.row_cells(4).empty(),
                prefix + " reopened row_cells should keep rejected D4 row empty");

            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_one =
                reopened_sheet.column_cells(1);
            check(reopened_column_one.size() == 2 &&
                    reopened_column_one[0].reference.row == 1 &&
                    reopened_column_one[0].reference.column == 1 &&
                    reopened_column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_one[0].value.text_value() == expected_a1 &&
                    reopened_column_one[1].reference.row == 2 &&
                    reopened_column_one[1].reference.column == 1 &&
                    reopened_column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_one[1].value.text_value() == "placeholder-a2",
                prefix + " reopened column_cells should expose overwritten A1 and source A2");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_two =
                reopened_sheet.column_cells(2);
            check(reopened_column_two.size() == 1 &&
                    reopened_column_two[0].reference.row == 1 &&
                    reopened_column_two[0].reference.column == 2 &&
                    reopened_column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_column_two[0].value.number_value() == 1.0,
                prefix + " reopened column_cells should expose source B1");
            check(reopened_sheet.column_cells(3).empty(),
                prefix + " reopened column_cells should keep the gap column empty");
            check(reopened_sheet.column_cells(4).empty(),
                prefix + " reopened column_cells should keep rejected D4 absent");
        });
}

void check_saved_default_data_overwrite_snapshots(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& handle,
    std::size_t expected_pending_count,
    std::string_view scenario,
    std::string_view expected_a1_text)
{
    const std::string prefix(scenario);
    const std::string expected_a1(expected_a1_text);

    check(handle.cell_count() == 3,
        prefix + " should keep the represented sparse count");
    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = handle.sparse_cells();
    check(cells.size() == 3,
        prefix + " should expose the three represented records");
    if (cells.size() == 3) {
        check(cells[0].reference.row == 1 &&
                cells[0].reference.column == 1 &&
                cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                cells[0].value.text_value() == expected_a1,
            prefix + " should keep overwritten A1 first");
        check(cells[1].reference.row == 1 &&
                cells[1].reference.column == 2 &&
                cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                cells[1].value.number_value() == 1.0,
            prefix + " should keep source-backed B1 second");
        check(cells[2].reference.row == 2 &&
                cells[2].reference.column == 1 &&
                cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                cells[2].value.text_value() == "placeholder-a2",
            prefix + " should keep source-backed A2 last");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
        handle.sparse_cells("A1:D4");
    check(range_cells.size() == 3,
        prefix + " range sparse_cells should expose the three represented records");
    if (range_cells.size() == 3) {
        check(range_cells[0].reference.row == 1 &&
                range_cells[0].reference.column == 1 &&
                range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                range_cells[0].value.text_value() == expected_a1,
            prefix + " range sparse_cells should keep overwritten A1 first");
        check(range_cells[1].reference.row == 1 &&
                range_cells[1].reference.column == 2 &&
                range_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                range_cells[1].value.number_value() == 1.0,
            prefix + " range sparse_cells should keep source-backed B1 second");
        check(range_cells[2].reference.row == 2 &&
                range_cells[2].reference.column == 1 &&
                range_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                range_cells[2].value.text_value() == "placeholder-a2",
            prefix + " range sparse_cells should keep source-backed A2 last");
    }
    const std::array<fastxlsx::WorksheetCellReference, 6> requested_refs {
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {3, 3},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
        handle.sparse_cells(requested_refs);
    check(requested_cells.size() == 4,
        prefix + " requested sparse_cells should skip rejected/gap coordinates and keep duplicates");
    if (requested_cells.size() == 4) {
        check(requested_cells[0].reference.row == 2 &&
                requested_cells[0].reference.column == 1 &&
                requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[0].value.text_value() == "placeholder-a2",
            prefix + " requested sparse_cells should keep A2 first");
        check(requested_cells[1].reference.row == 1 &&
                requested_cells[1].reference.column == 2 &&
                requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                requested_cells[1].value.number_value() == 1.0,
            prefix + " requested sparse_cells should keep B1 after skipped D4");
        check(requested_cells[2].reference.row == 1 &&
                requested_cells[2].reference.column == 1 &&
                requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[2].value.text_value() == expected_a1,
            prefix + " requested sparse_cells should keep overwritten A1 in requested order");
        check(requested_cells[3].reference.row == 2 &&
                requested_cells[3].reference.column == 1 &&
                requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[3].value.text_value() == "placeholder-a2",
            prefix + " requested sparse_cells should preserve duplicate A2");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = handle.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == expected_a1 &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        prefix + " should keep row-one overwritten text and source number");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two = handle.row_cells(2);
    check(row_two.size() == 1 &&
            row_two[0].reference.row == 2 &&
            row_two[0].reference.column == 1 &&
            row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_two[0].value.text_value() == "placeholder-a2",
        prefix + " should keep row-two source text");
    check(handle.row_cells(3).empty(),
        prefix + " should keep the gap row empty");
    check(handle.row_cells(4).empty(),
        prefix + " should keep rejected D4 row empty");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        handle.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[0].value.text_value() == expected_a1 &&
            column_one[1].reference.row == 2 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "placeholder-a2",
        prefix + " should keep column-one overwritten and source cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
        handle.column_cells(2);
    check(column_two.size() == 1 &&
            column_two[0].reference.row == 1 &&
            column_two[0].reference.column == 2 &&
            column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
            column_two[0].value.number_value() == 1.0,
        prefix + " should keep column-two source number");
    check(handle.column_cells(3).empty(),
        prefix + " should keep the gap column empty");
    check(handle.column_cells(4).empty(),
        prefix + " should keep rejected D4 absent");

    check(!handle.try_cell("D4").has_value(),
        prefix + " should keep rejected D4 absent");
    check_cell_range_equals(handle.used_range(), 1, 1, 2, 2,
        prefix + " should keep source bounds");
    check(!handle.has_pending_changes(),
        prefix + " should keep the handle clean");
    check(editor.pending_change_count() == expected_pending_count,
        prefix + " should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        prefix + " should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        prefix + " should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should keep dirty materialized memory empty");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should keep dirty summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should keep replacement diagnostics empty");
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear");
}

void check_saved_default_data_c3_snapshots(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& handle,
    std::size_t expected_pending_count,
    std::string_view scenario,
    fastxlsx::CellValueKind expected_c3_kind,
    std::string_view expected_c3_text)
{
    const std::string prefix(scenario);
    const std::string expected_text(expected_c3_text);

    check(handle.cell_count() == 4,
        prefix + " should keep the represented sparse count");
    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = handle.sparse_cells();
    check(cells.size() == 4,
        prefix + " should expose the four represented records");
    if (cells.size() == 4) {
        check(cells[0].reference.row == 1 &&
                cells[0].reference.column == 1 &&
                cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                cells[0].value.text_value() == "placeholder-a1",
            prefix + " should keep source-backed A1 first");
        check(cells[1].reference.row == 1 &&
                cells[1].reference.column == 2 &&
                cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                cells[1].value.number_value() == 1.0,
            prefix + " should keep source-backed B1 second");
        check(cells[2].reference.row == 2 &&
                cells[2].reference.column == 1 &&
                cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                cells[2].value.text_value() == "placeholder-a2",
            prefix + " should keep source-backed A2 third");
        check(cells[3].reference.row == 3 &&
                cells[3].reference.column == 3 &&
                cells[3].value.kind() == expected_c3_kind &&
                cells[3].value.text_value() == expected_text,
            prefix + " should keep later-wins C3 last");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
        handle.sparse_cells("A1:D4");
    check(range_cells.size() == 4,
        prefix + " range sparse_cells should expose all represented records");
    if (range_cells.size() == 4) {
        check(range_cells[0].reference.row == 1 &&
                range_cells[0].reference.column == 1 &&
                range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                range_cells[0].value.text_value() == "placeholder-a1",
            prefix + " range sparse_cells should keep source-backed A1 first");
        check(range_cells[1].reference.row == 1 &&
                range_cells[1].reference.column == 2 &&
                range_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                range_cells[1].value.number_value() == 1.0,
            prefix + " range sparse_cells should keep source-backed B1 second");
        check(range_cells[2].reference.row == 2 &&
                range_cells[2].reference.column == 1 &&
                range_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                range_cells[2].value.text_value() == "placeholder-a2",
            prefix + " range sparse_cells should keep source-backed A2 third");
        check(range_cells[3].reference.row == 3 &&
                range_cells[3].reference.column == 3 &&
                range_cells[3].value.kind() == expected_c3_kind &&
                range_cells[3].value.text_value() == expected_text,
            prefix + " range sparse_cells should keep later-wins C3 last");
    }
    const std::array<fastxlsx::WorksheetCellReference, 7> requested_refs {
        fastxlsx::WorksheetCellReference {3, 3},
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {3, 3},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {2, 3},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
        handle.sparse_cells(requested_refs);
    check(requested_cells.size() == 5,
        prefix + " requested sparse_cells should skip gap coordinates and keep duplicate C3");
    if (requested_cells.size() == 5) {
        check(requested_cells[0].reference.row == 3 &&
                requested_cells[0].reference.column == 3 &&
                requested_cells[0].value.kind() == expected_c3_kind &&
                requested_cells[0].value.text_value() == expected_text,
            prefix + " requested sparse_cells should keep C3 first");
        check(requested_cells[1].reference.row == 2 &&
                requested_cells[1].reference.column == 1 &&
                requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[1].value.text_value() == "placeholder-a2",
            prefix + " requested sparse_cells should keep A2 after skipped D4");
        check(requested_cells[2].reference.row == 1 &&
                requested_cells[2].reference.column == 2 &&
                requested_cells[2].value.kind() == fastxlsx::CellValueKind::Number &&
                requested_cells[2].value.number_value() == 1.0,
            prefix + " requested sparse_cells should keep B1 in requested order");
        check(requested_cells[3].reference.row == 3 &&
                requested_cells[3].reference.column == 3 &&
                requested_cells[3].value.kind() == expected_c3_kind &&
                requested_cells[3].value.text_value() == expected_text,
            prefix + " requested sparse_cells should preserve duplicate C3");
        check(requested_cells[4].reference.row == 1 &&
                requested_cells[4].reference.column == 1 &&
                requested_cells[4].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[4].value.text_value() == "placeholder-a1",
            prefix + " requested sparse_cells should keep A1 last");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = handle.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == "placeholder-a1" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        prefix + " should keep row-one source cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three = handle.row_cells(3);
    check(row_three.size() == 1 &&
            row_three[0].reference.row == 3 &&
            row_three[0].reference.column == 3 &&
            row_three[0].value.kind() == expected_c3_kind &&
            row_three[0].value.text_value() == expected_text,
        prefix + " should keep row-three later-wins C3");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two = handle.row_cells(2);
    check(row_two.size() == 1 &&
            row_two[0].reference.row == 2 &&
            row_two[0].reference.column == 1 &&
            row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_two[0].value.text_value() == "placeholder-a2",
        prefix + " should keep row-two source text");
    check(handle.row_cells(4).empty(),
        prefix + " should keep the trailing gap row empty");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        handle.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[0].value.text_value() == "placeholder-a1" &&
            column_one[1].reference.row == 2 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "placeholder-a2",
        prefix + " should keep column-one source cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        handle.column_cells(3);
    check(column_three.size() == 1 &&
            column_three[0].reference.row == 3 &&
            column_three[0].reference.column == 3 &&
            column_three[0].value.kind() == expected_c3_kind &&
            column_three[0].value.text_value() == expected_text,
        prefix + " should keep column-three later-wins C3");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
        handle.column_cells(2);
    check(column_two.size() == 1 &&
            column_two[0].reference.row == 1 &&
            column_two[0].reference.column == 2 &&
            column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
            column_two[0].value.number_value() == 1.0,
        prefix + " should keep column-two source number");
    check(handle.column_cells(4).empty(),
        prefix + " should keep the trailing gap column empty");

    check_cell_range_equals(handle.used_range(), 1, 1, 3, 3,
        prefix + " should keep expanded bounds");
    check(!handle.has_pending_changes(),
        prefix + " should keep the handle clean");
    check(editor.pending_change_count() == expected_pending_count,
        prefix + " should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        prefix + " should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        prefix + " should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should keep dirty materialized memory empty");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should keep dirty summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should keep replacement diagnostics empty");
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear");
}

void check_reopened_shift_output(
    const std::filesystem::path& output,
    std::string_view scenario,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data");
    const std::string prefix(scenario);

    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened output should not expose stale diagnostics");
    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened output should not expose dirty diagnostics");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened output should not expose dirty worksheet names");

    inspect(reopened_sheet);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened readback should keep public state clean");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened readback should keep dirty diagnostics empty");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened readback should keep dirty worksheet names empty");
    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened readback should keep last_edit_error empty");
}


void check_reopened_untouched_keep_me_output(
    const std::filesystem::path& output,
    std::string_view scenario)
{
    check_reopened_clean_sheet_output(
        output,
        "Untouched",
        scenario,
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 2,
                "reopened Untouched output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 2,
                "reopened Untouched output should keep source bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "keep-me",
                "reopened Untouched output should keep source-backed A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 99.0,
                "reopened Untouched output should keep source-backed B1");
        });
}

void check_reopened_styled_shift_source_output(
    const std::filesystem::path& source,
    fastxlsx::StyleId formula_style,
    std::string_view scenario)
{
    check_reopened_shift_output(
        source,
        scenario,
        [formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "reopened styled shift source should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 4,
                "reopened styled shift source should keep source bounds");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("B1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("A2").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("C2").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A3").text_value() == "extra-c3",
                "reopened styled shift source should keep source-backed cells");
            const std::optional<fastxlsx::CellValue> reopened_d2 =
                reopened_sheet.try_cell("D2");
            check(reopened_d2.has_value() &&
                    reopened_d2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d2->text_value() == "A1+B1" &&
                    reopened_d2->has_style() &&
                    reopened_d2->style_id().value() == formula_style.value(),
                "reopened styled shift source should keep original formula style");
            check(!reopened_sheet.try_cell("D4").has_value() &&
                    !reopened_sheet.try_cell("F2").has_value() &&
                    !reopened_sheet.try_cell("C5").has_value(),
                "reopened styled shift source should not contain shifted coordinates");
        });
    check_reopened_untouched_keep_me_output(source, scenario);
}

std::map<std::string, std::string> check_clean_shift_noop_save_output(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::filesystem::path& source,
    const std::map<std::string, std::string>& source_entries,
    const std::filesystem::path& previous_output,
    const std::map<std::string, std::string>& previous_entries,
    const std::filesystem::path& noop_output,
    std::string_view scenario,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    const std::string prefix(scenario);
    check(!sheet.has_pending_changes(),
        prefix + " should start from a clean materialized handle");
    const std::size_t pending_change_count_before_noop =
        editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes(),
        prefix + " should keep the materialized handle clean");
    check(editor.pending_change_count() == pending_change_count_before_noop,
        prefix + " should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should keep materialized diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, prefix);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, prefix);

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == previous_entries,
        prefix + " output should match the previous package");
    check(fastxlsx::test::read_zip_entries(previous_output) == previous_entries,
        prefix + " should leave the previous package unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " should leave the source package unchanged");
    check_reopened_shift_output(noop_output, prefix, inspect);
    check_reopened_untouched_keep_me_output(noop_output, prefix);

    return noop_entries;
}

void check_shift_post_noop_edit_save_output(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::filesystem::path& source,
    const std::map<std::string, std::string>& source_entries,
    const std::filesystem::path& output,
    const std::map<std::string, std::string>& output_entries,
    const std::filesystem::path& noop_output,
    const std::map<std::string, std::string>& noop_entries,
    const std::filesystem::path& second_noop_output,
    const std::map<std::string, std::string>& second_noop_entries,
    const std::filesystem::path& post_noop_output,
    std::string_view scenario,
    std::size_t expected_dirty_cell_count,
    const std::function<void()>& apply_edit,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    const std::string prefix(scenario);
    check(!sheet.has_pending_changes(),
        prefix + " should start from a clean materialized handle");
    const std::size_t pending_change_count_before_edit =
        editor.pending_change_count();

    apply_edit();

    check(sheet.has_pending_changes(),
        prefix + " should dirty the materialized handle");
    check(editor.pending_change_count() == pending_change_count_before_edit,
        prefix + " should not record a handoff before save");
    check(editor.pending_materialized_worksheet_names()
            == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == expected_dirty_cell_count &&
            editor.estimated_pending_materialized_memory_usage()
                == sheet.estimated_memory_usage(),
        prefix + " should report dirty materialized diagnostics");
    check(editor.pending_worksheet_edits().size() == 1,
        prefix + " should expose one dirty worksheet summary");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not queue replacement diagnostics before save");
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear before save");

    editor.save_as(post_noop_output);

    check(!sheet.has_pending_changes(),
        prefix + " should clean the materialized handle");
    check(editor.pending_change_count() == pending_change_count_before_edit + 1,
        prefix + " should record one additional materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should clear materialized diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should clear dirty worksheet summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not queue replacement diagnostics after save");
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear after save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        prefix + " should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        prefix + " should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output)
            == second_noop_entries,
        prefix + " should leave the second no-op output unchanged");

    check_reopened_shift_output(post_noop_output, prefix, inspect);
    check_reopened_untouched_keep_me_output(post_noop_output, prefix);
}

} // namespace
