// Structure tests for the public Patch-mode WorkbookEditor facade.
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

bool is_workbook_editor_shard(std::string_view shard)
{
    return shard == "all" || shard == "public-state";
}

std::string_view workbook_editor_shard_from_args(int argc, char* argv[])
{
    if (argc <= 1) {
        return "all";
    }
    if (argc != 2) {
        throw std::runtime_error(
            "usage: fastxlsx_workbook_editor_tests [--shard=<name>]");
    }

    std::string_view shard = argv[1];
    constexpr std::string_view prefix = "--shard=";
    if (shard.starts_with(prefix)) {
        shard.remove_prefix(prefix.size());
    }
    if (!is_workbook_editor_shard(shard)) {
        throw std::runtime_error("unknown workbook_editor shard: " + std::string(shard));
    }
    return shard;
}

bool should_run_workbook_editor_shard(std::string_view selected, std::string_view shard)
{
    return selected == "all" || selected == shard;
}

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

struct WorkbookEditorPublicSaveStateSnapshot {
    std::size_t pending_change_count{};
    std::size_t pending_replacement_cell_count{};
    std::size_t estimated_pending_replacement_memory_usage{};
    std::vector<std::string> pending_replacement_worksheet_names;
    std::optional<std::string> last_edit_error;
};

WorkbookEditorPublicSaveStateSnapshot workbook_editor_public_save_state_snapshot(
    const fastxlsx::WorkbookEditor& editor)
{
    return {
        editor.pending_change_count(),
        editor.pending_replacement_cell_count(),
        editor.estimated_pending_replacement_memory_usage(),
        editor.pending_replacement_worksheet_names(),
        editor.last_edit_error(),
    };
}

void check_workbook_editor_public_save_state_preserved(
    const fastxlsx::WorkbookEditor& editor,
    const WorkbookEditorPublicSaveStateSnapshot& before,
    std::string_view scenario)
{
    check(editor.pending_change_count() == before.pending_change_count,
        std::string(scenario) + " should preserve public pending change count");
    check(editor.pending_replacement_cell_count()
            == before.pending_replacement_cell_count,
        std::string(scenario) + " should preserve pending replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage()
            == before.estimated_pending_replacement_memory_usage,
        std::string(scenario) + " should preserve replacement memory estimate");
    check(editor.pending_replacement_worksheet_names()
            == before.pending_replacement_worksheet_names,
        std::string(scenario) + " should preserve pending replacement worksheet names");
    check(editor.last_edit_error() == before.last_edit_error,
        std::string(scenario) + " should not replace or clear last_edit_error");
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
    (void)editor.worksheet_names();
    check(editor.last_edit_error() == expected,
        "worksheet_names should not update last_edit_error");

    (void)editor.has_worksheet("Data");
    check(editor.last_edit_error() == expected,
        "has_worksheet should not update last_edit_error");

    (void)editor.source_worksheet_names();
    check(editor.last_edit_error() == expected,
        "source_worksheet_names should not update last_edit_error");

    (void)editor.has_source_worksheet("Data");
    check(editor.last_edit_error() == expected,
        "has_source_worksheet should not update last_edit_error");

    (void)editor.has_pending_changes();
    check(editor.last_edit_error() == expected,
        "has_pending_changes should not update last_edit_error");

    (void)editor.pending_change_count();
    check(editor.last_edit_error() == expected,
        "pending_change_count should not update last_edit_error");

    (void)editor.pending_replacement_cell_count();
    check(editor.last_edit_error() == expected,
        "pending_replacement_cell_count should not update last_edit_error");

    (void)editor.pending_replacement_worksheet_names();
    check(editor.last_edit_error() == expected,
        "pending_replacement_worksheet_names should not update last_edit_error");

    (void)editor.pending_materialized_worksheet_names();
    check(editor.last_edit_error() == expected,
        "pending_materialized_worksheet_names should not update last_edit_error");

    (void)editor.pending_materialized_cell_count();
    check(editor.last_edit_error() == expected,
        "pending_materialized_cell_count should not update last_edit_error");

    (void)editor.estimated_pending_materialized_memory_usage();
    check(editor.last_edit_error() == expected,
        "estimated_pending_materialized_memory_usage should not update last_edit_error");

    (void)editor.has_pending_replacement("Data");
    check(editor.last_edit_error() == expected,
        "has_pending_replacement should not update last_edit_error");

    (void)editor.estimated_pending_replacement_memory_usage();
    check(editor.last_edit_error() == expected,
        "estimated_pending_replacement_memory_usage should not update last_edit_error");

    (void)editor.pending_worksheet_edits();
    check(editor.last_edit_error() == expected,
        "pending_worksheet_edits should not update last_edit_error");

    (void)editor.worksheet_catalog();
    check(editor.last_edit_error() == expected,
        "worksheet_catalog should not update last_edit_error");

    (void)editor.formula_reference_audits();
    check(editor.last_edit_error() == expected,
        "formula_reference_audits should not update last_edit_error");

    (void)editor.source_formula_reference_audits();
    check(editor.last_edit_error() == expected,
        "source_formula_reference_audits should not update last_edit_error");

    (void)editor.defined_name_formula_reference_audits();
    check(editor.last_edit_error() == expected,
        "defined_name_formula_reference_audits should not update last_edit_error");
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

void test_public_worksheet_editor_has_pending_changes_tracks_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-dirty-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-dirty-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-dirty-second.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(!sheet.has_pending_changes(),
        "freshly materialized public WorksheetEditor should start clean");
    check(!editor.has_pending_changes(),
        "clean materialized WorksheetEditor should not make save_as pending");

    (void)sheet.try_cell(1, 1);
    (void)sheet.get_cell(1, 1);
    (void)sheet.cell_count();
    (void)sheet.estimated_memory_usage();
    (void)sheet.sparse_cells();
    (void)sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    check(!sheet.has_pending_changes(),
        "read-only WorksheetEditor APIs should not dirty the materialized session");
    check(!editor.last_edit_error().has_value(),
        "read-only WorksheetEditor APIs should not update last_edit_error");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-reference"));
    }), "invalid A1 mutation should fail before dirtying the worksheet");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "failed WorksheetEditor mutation should set last_edit_error");

    check(!sheet.has_pending_changes(),
        "failed WorksheetEditor mutation should preserve clean dirty state");
    (void)sheet.has_pending_changes();
    (void)sheet.try_cell(1, 1);
    (void)sheet.get_cell(1, 1);
    (void)sheet.cell_count();
    (void)sheet.estimated_memory_usage();
    (void)sheet.sparse_cells();
    (void)sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 2});
    check(editor.last_edit_error() == prior_error,
        "WorksheetEditor dirty/read inspection should not update last_edit_error");

    sheet.erase_cell(5, 5);
    check(!sheet.has_pending_changes(),
        "erasing a missing sparse record should keep a clean worksheet clean");
    check(!editor.has_pending_changes(),
        "missing-cell erase should not make the WorkbookEditor save_as pending");
    check(!editor.last_edit_error().has_value(),
        "successful missing-cell erase should clear prior mutation diagnostics");

    sheet.erase_cell(2, 1);
    check(sheet.has_pending_changes(),
        "erasing an existing sparse record should dirty the worksheet session");
    check(editor.has_pending_changes(),
        "dirty WorksheetEditor should make WorkbookEditor save_as pending");
    check(editor.pending_change_count() == 0,
        "dirty WorksheetEditor should not count as a queued Patch handoff before flush");

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as path preflight failure should run before dirty-session flush");
    check(sheet.has_pending_changes(),
        "save_as path preflight failure should preserve dirty WorksheetEditor state");
    check(editor.pending_change_count() == 0,
        "save_as path preflight failure should not queue a materialized handoff");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "successful save_as should clear the flushed WorksheetEditor dirty state");
    check(editor.pending_change_count() == 1,
        "successful save_as should count one materialized Patch handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(first_xml, "placeholder-a2",
        "flushed dirty WorksheetEditor state should persist erased source cells");

    sheet.set_cell(3, 3, fastxlsx::CellValue::text("dirty-again"));
    check(sheet.has_pending_changes(),
        "WorksheetEditor should become dirty again after a post-save mutation");
    editor.save_as(second_output);

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"), "dirty-again",
        "post-save dirty WorksheetEditor mutation should persist on a later save_as");
}

void test_public_worksheet_editor_handle_remains_valid_after_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-handle-save-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-handle-save-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-handle-save-second.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("same-handle-first-save"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "failed save_as path guard should not invalidate a borrowed WorksheetEditor handle");
    check(sheet.has_pending_changes(),
        "failed save_as should keep the same WorksheetEditor handle dirty and valid");
    check(editor.pending_change_count() == 0,
        "failed save_as should not flush the dirty materialized session");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "successful save_as should keep the same WorksheetEditor handle valid and clean");
    const fastxlsx::CellValue saved_value = sheet.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "same-handle-first-save",
        "same WorksheetEditor handle should still read materialized cells after save_as");

    sheet.set_cell(1, 2, fastxlsx::CellValue::text("same-handle-second-save"));
    check(sheet.has_pending_changes(),
        "same WorksheetEditor handle should be reusable for post-save edits");
    editor.save_as(second_output);
    check(editor.pending_change_count() == 2,
        "second save_as should record a second materialized Patch handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"), "same-handle-first-save",
        "first output should contain the first same-handle materialized edit");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "same-handle-second-save",
        "later same-handle edits should not mutate an earlier output artifact");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"), "same-handle-first-save",
        "second output should retain prior materialized sparse state");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"), "same-handle-second-save",
        "second output should contain the post-save same-handle edit");
}

void test_public_workbook_editor_pending_materialized_names_track_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-names-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-materialized-names-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.pending_materialized_worksheet_names().empty(),
        "fresh WorkbookEditor should expose no pending materialized worksheet names");

    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean materialized sessions should not appear in pending materialized names");

    check(threw_fastxlsx_error([&] {
        data.set_cell("a1", fastxlsx::CellValue::text("invalid-reference"));
    }), "failed WorksheetEditor mutation should throw before pending materialized diagnostics");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "failed WorksheetEditor mutation should set last_edit_error before diagnostics");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed WorksheetEditor mutation should not add a dirty materialized name");
    check(editor.last_edit_error() == prior_error,
        "pending materialized name inspection should not update last_edit_error");

    data.set_cell(1, 1, fastxlsx::CellValue::text("dirty-data"));
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "one dirty materialized session should report its planned sheet name");
    }
    check(editor.pending_change_count() == 0,
        "dirty materialized names should not count as Patch handoffs before save_as");

    untouched.set_cell(1, 2, fastxlsx::CellValue::text("dirty-untouched"));
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 2,
            "two dirty materialized sessions should both be reported");
        if (names.size() == 2) {
            check(names[0] == "Data",
                "dirty materialized names should follow planned catalog order");
            check(names[1] == "Untouched",
                "dirty materialized names should include the second planned sheet");
        }
    }

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as path preflight failure should not flush dirty materialized sessions");
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 2 && names[0] == "Data" && names[1] == "Untouched",
            "failed save_as should preserve dirty materialized names");
    }

    editor.save_as(output);
    check(editor.pending_materialized_worksheet_names().empty(),
        "successful save_as should clear dirty materialized names");
    check(editor.pending_change_count() == 2,
        "successful save_as should count both materialized Patch handoffs");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "dirty-data",
        "first dirty materialized worksheet should persist through save_as");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "dirty-untouched",
        "second dirty materialized worksheet should persist through save_as");
}

void test_public_workbook_editor_pending_materialized_names_move_with_owner()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-names-move-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-names-move-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-materialized-names-move-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Data");
    source_sheet.set_cell(1, 1, fastxlsx::CellValue::text("moved-dirty-data"));

    fastxlsx::WorkbookEditor moved = std::move(editor);
    check(editor.pending_materialized_worksheet_names().empty(),
        "moved-from editor should expose no pending materialized names");
    {
        const std::vector<std::string> names = moved.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "move construction should preserve dirty materialized names");
    }

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::WorksheetEditor target_sheet = target.worksheet("Untouched");
    target_sheet.set_cell(1, 1, fastxlsx::CellValue::text("discarded-target-dirty"));
    {
        const std::vector<std::string> names = target.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Untouched",
            "target should start with its own dirty materialized name");
    }

    target = std::move(moved);
    check(moved.pending_materialized_worksheet_names().empty(),
        "move-assigned-from editor should expose no pending materialized names");
    {
        const std::vector<std::string> names = target.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "move assignment should replace target dirty materialized names");
    }

    target.save_as(output);
    check(target.pending_materialized_worksheet_names().empty(),
        "save_as after move assignment should clear assigned dirty materialized names");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "moved-dirty-data",
        "move-assigned editor should save assigned dirty materialized payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet2.xml"), "discarded-target-dirty",
        "move assignment should not leak discarded target dirty materialized payload");
}

void test_public_workbook_editor_pending_materialized_aggregate_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-aggregate-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-materialized-aggregate-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
    check(editor.pending_materialized_cell_count() == 0,
        "clean materialized sessions should not contribute to pending materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "clean materialized sessions should not contribute to pending materialized memory");

    check(threw_fastxlsx_error([&] {
        data.set_cell("a1", fastxlsx::CellValue::text("invalid-reference"));
    }), "failed mutation should throw before materialized aggregate diagnostics change");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(editor.pending_materialized_cell_count() == 0,
        "failed mutation should not add pending materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed mutation should not add pending materialized memory");
    check(editor.last_edit_error() == prior_error,
        "materialized aggregate diagnostics should not update last_edit_error");

    const std::size_t data_clean_memory = data.estimated_memory_usage();
    data.set_cell(1, 1, fastxlsx::CellValue::text("aggregate-dirty-data"));
    data.set_cell(3, 3, fastxlsx::CellValue::blank());
    check(data.cell_count() == 4,
        "dirty data worksheet should include source cells plus explicit blank record");
    check(editor.pending_materialized_cell_count() == data.cell_count(),
        "one dirty materialized session should contribute its sparse cell count");
    check(editor.estimated_pending_materialized_memory_usage() == data.estimated_memory_usage(),
        "one dirty materialized session should contribute its memory estimate");
    check(editor.estimated_pending_materialized_memory_usage() >= data_clean_memory,
        "dirty materialized memory estimate should stay non-zero after mutation");
    check(editor.pending_change_count() == 0,
        "materialized aggregate diagnostics should not increment Patch handoff count");

    untouched.set_cell(1, 2, fastxlsx::CellValue::text("aggregate-dirty-untouched"));
    const std::size_t expected_dirty_cells = data.cell_count() + untouched.cell_count();
    const std::size_t expected_dirty_memory =
        data.estimated_memory_usage() + untouched.estimated_memory_usage();
    check(editor.pending_materialized_cell_count() == expected_dirty_cells,
        "two dirty materialized sessions should aggregate sparse cell counts");
    check(editor.estimated_pending_materialized_memory_usage() == expected_dirty_memory,
        "two dirty materialized sessions should aggregate memory estimates");

    const std::filesystem::path replacement_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-aggregate-replacement-source.xlsx");
    fastxlsx::WorkbookEditor replacement_editor =
        fastxlsx::WorkbookEditor::open(replacement_source);
    replacement_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("ignored")}});
    check(replacement_editor.pending_replacement_cell_count() == 1,
        "replacement-only editor should expose queued replacement diagnostics");
    check(replacement_editor.pending_materialized_cell_count() == 0,
        "queued replacement diagnostics should not contribute materialized cells");
    check(replacement_editor.estimated_pending_materialized_memory_usage() == 0,
        "queued replacement diagnostics should not contribute materialized memory");

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "failed save_as should preserve dirty materialized aggregate diagnostics");
    check(editor.pending_materialized_cell_count() == expected_dirty_cells,
        "failed save_as should preserve materialized cell aggregate");
    check(editor.estimated_pending_materialized_memory_usage() == expected_dirty_memory,
        "failed save_as should preserve materialized memory aggregate");

    editor.save_as(output);
    check(editor.pending_materialized_cell_count() == 0,
        "successful save_as should clear dirty materialized cell aggregate");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "successful save_as should clear dirty materialized memory aggregate");
    check(editor.pending_replacement_cell_count() == 0,
        "materialized auto-flush should not be reported as whole-sheetData replacements");
    check(editor.pending_change_count() == 2,
        "successful save_as should count both materialized Patch handoffs");
}

void test_public_workbook_editor_pending_materialized_aggregate_moves_with_owner()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-aggregate-move-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-aggregate-move-target.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Data");
    source_sheet.set_cell(1, 1, fastxlsx::CellValue::text("moved-aggregate-dirty"));
    const std::size_t moved_cells = editor.pending_materialized_cell_count();
    const std::size_t moved_memory = editor.estimated_pending_materialized_memory_usage();
    check(moved_cells == source_sheet.cell_count() && moved_memory > 0,
        "source editor should expose aggregate diagnostics before move");

    fastxlsx::WorkbookEditor moved = std::move(editor);
    check(editor.pending_materialized_cell_count() == 0,
        "moved-from editor should expose zero pending materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "moved-from editor should expose zero pending materialized memory");
    check(moved.pending_materialized_cell_count() == moved_cells,
        "move construction should preserve materialized cell aggregate");
    check(moved.estimated_pending_materialized_memory_usage() == moved_memory,
        "move construction should preserve materialized memory aggregate");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::WorksheetEditor target_sheet = target.worksheet("Untouched");
    target_sheet.set_cell(1, 1, fastxlsx::CellValue::text("discarded-aggregate-dirty"));
    check(target.pending_materialized_cell_count() == target_sheet.cell_count(),
        "target should start with its own materialized aggregate");

    target = std::move(moved);
    check(moved.pending_materialized_cell_count() == 0,
        "move-assigned-from editor should expose zero pending materialized cells");
    check(moved.estimated_pending_materialized_memory_usage() == 0,
        "move-assigned-from editor should expose zero pending materialized memory");
    check(target.pending_materialized_cell_count() == moved_cells,
        "move assignment should replace target materialized cell aggregate");
    check(target.estimated_pending_materialized_memory_usage() == moved_memory,
        "move assignment should replace target materialized memory aggregate");
}

void test_public_workbook_editor_pending_summaries_include_materialized_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-summary-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-materialized-summary-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    check(editor.pending_worksheet_edits().empty(),
        "clean materialized session should not create pending worksheet summaries");

    const std::optional<std::string> no_error_before_dirty = editor.last_edit_error();
    data.set_cell(1, 1, fastxlsx::CellValue::text("summary-dirty-data"));
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "one dirty materialized session should create one worksheet summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "materialized summary should report the source sheet name");
            check(summary.planned_name == "Data",
                "materialized summary should report the current planned sheet name");
            check(!summary.renamed,
                "materialized-only summary should not be marked as renamed");
            check(!summary.sheet_data_replaced,
                "materialized-only summary should not report sheetData replacement");
            check(summary.materialized_dirty,
                "materialized-only summary should report dirty materialized state");
            check(summary.replacement_cell_count == 0,
                "materialized-only summary should report zero replacement cells");
            check(summary.estimated_replacement_memory_usage == 0,
                "materialized-only summary should report zero replacement memory");
            check(summary.materialized_cell_count == 3,
                "materialized summary should report active sparse materialized cells");
            check(summary.estimated_materialized_memory_usage > 0,
                "materialized summary should report materialized memory estimate");
        }
    }
    check(editor.pending_change_count() == 0,
        "dirty materialized summaries should not increment pending_change_count before save");
    check(editor.last_edit_error() == no_error_before_dirty,
        "pending materialized summary inspection should not update last_edit_error");

    editor.replace_sheet_data("Untouched", {{fastxlsx::CellValue::text("replacement")}});
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 2,
            "dirty materialized and cross-sheet replacement summaries should coexist");
        if (summaries.size() == 2) {
            check(summaries[0].source_name == "Data" && summaries[0].materialized_dirty,
                "dirty materialized summary should stay in source order");
            check(!summaries[0].sheet_data_replaced,
                "dirty materialized summary should not be marked as replacement");
            check(summaries[1].source_name == "Untouched" &&
                    summaries[1].sheet_data_replaced,
                "cross-sheet replacement summary should follow in source order");
            check(!summaries[1].materialized_dirty,
                "replacement-only summary should not be marked materialized dirty");
            check(summaries[1].materialized_cell_count == 0,
                "replacement-only summary should report zero materialized cell count");
            check(summaries[1].estimated_materialized_memory_usage == 0,
                "replacement-only summary should report zero materialized memory");
        }
    }

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "failed save_as should preserve dirty materialized summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 2,
            "failed save_as should preserve materialized and replacement summary count");
        if (summaries.size() == 2) {
            check(summaries[0].materialized_dirty,
                "failed save_as should preserve materialized dirty flag");
            check(summaries[1].sheet_data_replaced,
                "failed save_as should preserve replacement summary flag");
        }
    }

    editor.save_as(output);
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "successful save_as should remove dirty materialized summary after auto-flush");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Untouched",
                "remaining summary should be the queued cross-sheet replacement");
            check(summaries[0].sheet_data_replaced,
                "remaining summary should preserve replacement flag");
            check(!summaries[0].materialized_dirty,
                "successful save_as should clear materialized dirty flag from summaries");
        }
    }
}

void test_public_workbook_editor_pending_materialized_summaries_move_with_owner()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-summary-move-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-summary-move-target.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Data");
    source_sheet.set_cell(1, 1, fastxlsx::CellValue::text("moved-summary-dirty"));

    fastxlsx::WorkbookEditor moved = std::move(editor);
    check(editor.pending_worksheet_edits().empty(),
        "moved-from editor should expose no pending materialized summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            moved.pending_worksheet_edits();
        check(summaries.size() == 1 && summaries[0].planned_name == "Data",
            "move construction should preserve materialized dirty summary");
        if (summaries.size() == 1) {
            check(summaries[0].materialized_dirty,
                "move construction should preserve materialized dirty flag");
            check(summaries[0].materialized_cell_count == 3,
                "move construction should preserve materialized cell count");
        }
    }

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::WorksheetEditor target_sheet = target.worksheet("Untouched");
    target_sheet.set_cell(1, 1, fastxlsx::CellValue::text("discarded-summary-dirty"));

    target = std::move(moved);
    check(moved.pending_worksheet_edits().empty(),
        "move-assigned-from editor should expose no pending materialized summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            target.pending_worksheet_edits();
        check(summaries.size() == 1 && summaries[0].planned_name == "Data",
            "move assignment should replace target materialized summaries");
        if (summaries.size() == 1) {
            check(summaries[0].materialized_dirty,
                "move assignment should preserve assigned materialized dirty flag");
            check(summaries[0].materialized_cell_count == 3,
                "move assignment should discard old target materialized summary");
        }
    }
}

void test_public_worksheet_editor_get_cell_missing_and_blank_semantics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-get-cell-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before_missing_read = sheet.cell_count();
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(4, 4); }),
        "get_cell should throw for a missing sparse cell");
    check(sheet.cell_count() == cell_count_before_missing_read,
        "failed get_cell should not mutate the sparse store");
    check(!editor.last_edit_error().has_value(),
        "failed get_cell missing-cell read should not update last_edit_error");

    sheet.set_cell(4, 4, fastxlsx::CellValue::blank());
    const fastxlsx::CellValue explicit_blank = sheet.get_cell(4, 4);
    check(explicit_blank.kind() == fastxlsx::CellValueKind::Blank,
        "get_cell should return explicit blank records distinctly from missing cells");
    check(sheet.try_cell(5, 5) == std::nullopt,
        "try_cell should continue to report unrelated missing cells as nullopt");
}

void test_public_worksheet_editor_a1_overloads_read_mutate_and_save()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-a1-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-a1-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> maybe_a1 = sheet.try_cell("A1");
    check(maybe_a1.has_value() && maybe_a1->kind() == fastxlsx::CellValueKind::Text &&
            maybe_a1->text_value() == "placeholder-a1",
        "A1 try_cell overload should read source-backed text");

    const fastxlsx::CellValue b1 = sheet.get_cell("B1");
    check(b1.kind() == fastxlsx::CellValueKind::Number && b1.number_value() == 1.0,
        "A1 get_cell overload should read source-backed numbers");

    sheet.set_cell("D4", fastxlsx::CellValue::text("a1-overload-new"));
    const fastxlsx::CellValue d4 = sheet.get_cell("D4");
    check(d4.kind() == fastxlsx::CellValueKind::Text &&
            d4.text_value() == "a1-overload-new",
        "A1 set_cell overload should update the materialized sparse store");

    sheet.erase_cell("A2");
    check(sheet.try_cell("A2") == std::nullopt,
        "A1 erase_cell overload should remove the sparse record");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "A1 overload save_as should refresh dimension through the existing handoff");
    check_contains(worksheet_xml, R"(<c r="D4" t="inlineStr">)",
        "A1 overload set_cell should persist the target cell reference");
    check_contains(worksheet_xml, "a1-overload-new",
        "A1 overload set_cell should persist the target text");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "A1 overload erase_cell should persist the erased source cell");
}

void test_public_worksheet_editor_a1_overloads_reject_invalid_references()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-a1-invalid-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before_reads = sheet.cell_count();
    const std::array<std::string_view, 8> invalid_references {
        "", "a1", "1A", "A0", "A01", "XFE1", "A1048577", "A1:B2"};

    for (const std::string_view reference : invalid_references) {
        check(threw_fastxlsx_error([&] { (void)sheet.try_cell(reference); }),
            "A1 try_cell overload should reject invalid references");
        check(threw_fastxlsx_error([&] { (void)sheet.get_cell(reference); }),
            "A1 get_cell overload should reject invalid references");
    }

    check(sheet.cell_count() == cell_count_before_reads,
        "invalid A1 read overloads should not mutate the sparse store");
    check(!sheet.try_cell("XFD1048576").has_value(),
        "A1 try_cell overload should accept the last legal Excel cell reference");
    check(!editor.last_edit_error().has_value(),
        "invalid A1 read overloads should not update last_edit_error");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 read overloads should preserve existing cells");

    const std::size_t cell_count_before_mutation = sheet.cell_count();
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("should-not-write"));
    }), "A1 set_cell overload should reject lowercase references");
    check(sheet.cell_count() == cell_count_before_mutation,
        "invalid A1 set_cell should not mutate sparse cell count");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 set_cell should not overwrite source cells");
    check(editor.last_edit_error().has_value(),
        "invalid A1 set_cell should update last_edit_error");

    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "A1 erase_cell overload should reject range references");
    check(sheet.cell_count() == cell_count_before_mutation,
        "invalid A1 erase_cell should not mutate sparse cell count");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 erase_cell should not remove source cells");
    check(editor.last_edit_error().has_value(),
        "invalid A1 erase_cell should update last_edit_error");
}

void test_public_worksheet_editor_a1_range_mutations_sparse_semantics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-a1-range-mutation-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-a1-range-mutation-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell("C3", fastxlsx::CellValue::text("a1-range-clear-target"));
    sheet.set_cell("D4", fastxlsx::CellValue::text("outside-a1-range"));

    sheet.clear_cell_values("B1:C3");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "A1 range clear should blank represented B1");
    check(sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Blank,
        "A1 range clear should blank represented C3");
    check(!sheet.try_cell("B2").has_value(),
        "A1 range clear should not synthesize missing cells inside the rectangle");

    const std::vector<fastxlsx::WorksheetCellSnapshot> cleared_range =
        sheet.sparse_cells("B1:C3");
    check(cleared_range.size() == 2,
        "A1 range clear should keep the mutation sparse, not dense");
    if (cleared_range.size() == 2) {
        check(cleared_range[0].reference.row == 1 && cleared_range[0].reference.column == 2 &&
                cleared_range[0].value.kind() == fastxlsx::CellValueKind::Blank,
            "A1 range clear should expose the blanked B1 record");
        check(cleared_range[1].reference.row == 3 && cleared_range[1].reference.column == 3 &&
                cleared_range[1].value.kind() == fastxlsx::CellValueKind::Blank,
            "A1 range clear should expose the blanked C3 record");
    }

    sheet.erase_cells("A1:A2");
    check(!sheet.try_cell("A1").has_value(),
        "A1 range erase should remove represented A1");
    check(!sheet.try_cell("A2").has_value(),
        "A1 range erase should remove represented A2");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "A1 range erase should leave unrelated blanked B1 intact");
    check(sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Blank,
        "A1 range erase should leave unrelated blanked C3 intact");

    sheet.erase_cells("B2:C2");
    check(!editor.last_edit_error().has_value(),
        "missing-only A1 range erase should be a successful no-op");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="B1"/>)",
        "A1 range clear should persist the B1 explicit blank");
    check_contains(worksheet_xml, R"(<c r="C3"/>)",
        "A1 range clear should persist the C3 explicit blank");
    check_contains(worksheet_xml, "outside-a1-range",
        "A1 range mutations should preserve cells outside the selected range");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "A1 range erase should omit erased A1 source text");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "A1 range erase should omit erased A2 source text");
    check_not_contains(worksheet_xml, "a1-range-clear-target",
        "A1 range clear should omit the prior C3 text payload");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "A1 range mutations should preserve untouched worksheets");
}

void test_public_worksheet_editor_a1_range_mutations_invalid_references()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-a1-range-invalid-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-a1-range-invalid-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before = sheet.cell_count();
    const std::size_t memory_before = sheet.estimated_memory_usage();
    const std::array<std::string_view, 13> invalid_range_references {
        "",
        "a1:B2",
        "A1:b2",
        "A0:B2",
        "A01:B2",
        "XFE1:XFE2",
        "A1:A1048577",
        "A1:B2:C3",
        "B2:A1",
        "A:C",
        "1:3",
        "$A$1:$B$2",
        "Data!A1:B2",
    };

    for (const std::string_view invalid_reference : invalid_range_references) {
        check(threw_fastxlsx_error([&] { sheet.clear_cell_values(invalid_reference); }),
            "A1 range clear should reject invalid references");
        check(editor.last_edit_error().has_value(),
            "invalid A1 range clear should update last_edit_error");
        check(threw_fastxlsx_error([&] { sheet.erase_cells(invalid_reference); }),
            "A1 range erase should reject invalid references");
        check(editor.last_edit_error().has_value(),
            "invalid A1 range erase should update last_edit_error");
    }

    check(!sheet.has_pending_changes(),
        "invalid A1 range mutations should not dirty the materialized sheet");
    check(!editor.has_pending_changes(),
        "invalid A1 range mutations should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "invalid A1 range mutations should not increment public edit count");
    check(sheet.cell_count() == cell_count_before,
        "invalid A1 range mutations should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == memory_before,
        "invalid A1 range mutations should preserve sparse memory estimate");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 range mutations should preserve existing source cells");

    sheet.clear_cell_values("Z10:Z11");
    check(!editor.last_edit_error().has_value(),
        "valid missing-only A1 range clear should clear prior diagnostics");
    check(!sheet.has_pending_changes(),
        "valid missing-only A1 range clear should remain clean");

    check(threw_fastxlsx_error([&] { sheet.erase_cells("A0:A1"); }),
        "invalid A1 range erase should seed a diagnostic before recovery");
    check(editor.last_edit_error().has_value(),
        "invalid A1 range erase should record a diagnostic before recovery");
    sheet.erase_cells("Z10:Z11");
    check(!editor.last_edit_error().has_value(),
        "valid missing-only A1 range erase should clear prior diagnostics");
    check(!sheet.has_pending_changes(),
        "valid missing-only A1 range erase should remain clean");
    check(sheet.cell_count() == cell_count_before,
        "valid missing-only A1 range mutations should not synthesize cells");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as after invalid A1 range mutations should copy source entries");
}

void test_public_worksheet_editor_row_column_overloads_reject_invalid_coordinates()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-row-column-invalid-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-row-column-invalid-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before_reads = sheet.cell_count();
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "row/column try_cell should reject row zero");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(1, 0); }),
        "row/column get_cell should reject column zero");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(1048577, 1); }),
        "row/column try_cell should reject rows beyond Excel limits");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(1, 16385); }),
        "row/column get_cell should reject columns beyond Excel limits");
    check(sheet.cell_count() == cell_count_before_reads,
        "invalid row/column reads should not mutate sparse cell count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "row/column try_cell should accept the last legal Excel coordinate");
    check(!editor.last_edit_error().has_value(),
        "invalid row/column reads should not update last_edit_error");

    const std::size_t cell_count_before_mutations = sheet.cell_count();
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-row-zero"));
    }), "row/column set_cell should reject row zero");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(1, 16385, fastxlsx::CellValue::text("invalid-column-overflow"));
    }), "row/column set_cell should reject columns beyond Excel limits");
    check(threw_fastxlsx_error([&] { sheet.erase_cell(1048577, 1); }),
        "row/column erase_cell should reject rows beyond Excel limits");
    check(threw_fastxlsx_error([&] { sheet.erase_cell(1, 0); }),
        "row/column erase_cell should reject column zero");
    check(sheet.cell_count() == cell_count_before_mutations,
        "invalid row/column mutations should not mutate sparse cell count");
    check(!sheet.has_pending_changes(),
        "invalid row/column mutations should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "invalid row/column mutations should not make save_as pending");
    check(editor.last_edit_error().has_value(),
        "invalid row/column mutations should update last_edit_error");
    check(sheet.get_cell(1, 1).text_value() == "placeholder-a1",
        "invalid row/column mutations should preserve existing cells");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("row-column-recovered"));
    check(!editor.last_edit_error().has_value(),
        "valid row/column mutation should clear prior mutation diagnostics");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "row-column-recovered",
        "valid row/column mutation after invalid coordinates should still persist");
    check_not_contains(worksheet_xml, "invalid-row-zero",
        "invalid row/column set_cell payload should not leak into saved XML");
    check_not_contains(worksheet_xml, "invalid-column-overflow",
        "invalid overflow-column set_cell payload should not leak into saved XML");
}

void test_public_worksheet_editor_invalid_cell_reads_preserve_prior_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-invalid-read-error-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-invalid-read-error-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before = sheet.cell_count();
    const std::size_t memory_before = sheet.estimated_memory_usage();
    const std::vector<fastxlsx::WorksheetCellSnapshot> snapshot_before = sheet.sparse_cells();
    check(cell_count_before == 3 && snapshot_before.size() == 3,
        "invalid-read diagnostic test should start from the source sparse cells");
    check(!sheet.has_pending_changes(),
        "invalid-read diagnostic test should start with a clean sheet");
    check(!editor.has_pending_changes(),
        "invalid-read diagnostic test should start with a clean editor");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-read-sentinel"));
    }), "invalid coordinate mutation should seed last_edit_error before read failures");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "invalid coordinate mutation should record a prior diagnostic");
    if (prior_error.has_value()) {
        check_contains(*prior_error, "WorksheetEditor cell coordinate is invalid",
            "prior diagnostic should be the seeded invalid coordinate failure");
    }

    const auto check_preserved = [&] (std::string_view prefix) {
        check(editor.last_edit_error() == prior_error,
            std::string(prefix) + " should preserve prior last_edit_error");
        check(!sheet.has_pending_changes(),
            std::string(prefix) + " should not dirty the materialized sheet");
        check(!editor.has_pending_changes(),
            std::string(prefix) + " should not dirty the editor");
        check(editor.pending_change_count() == 0,
            std::string(prefix) + " should not increment public edit count");
        check(sheet.cell_count() == cell_count_before,
            std::string(prefix) + " should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == memory_before,
            std::string(prefix) + " should preserve sparse memory estimate");
    };

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "row/column try_cell read should reject row zero");
    check_preserved("row-zero try_cell read");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(1, 0); }),
        "row/column get_cell read should reject column zero");
    check_preserved("column-zero get_cell read");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(1048577, 1); }),
        "row/column try_cell read should reject row overflow");
    check_preserved("row-overflow try_cell read");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(1, 16385); }),
        "row/column get_cell read should reject column overflow");
    check_preserved("column-overflow get_cell read");

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "A1 try_cell read should reject lowercase references");
    check_preserved("lowercase A1 try_cell read");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell("A1:B2"); }),
        "A1 get_cell read should reject range references");
    check_preserved("range A1 get_cell read");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("A01"); }),
        "A1 try_cell read should reject leading-zero rows");
    check_preserved("leading-zero A1 try_cell read");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell("XFE1"); }),
        "A1 get_cell read should reject column overflow");
    check_preserved("column-overflow A1 get_cell read");

    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(4, 4); }),
        "get_cell read should reject a valid but missing sparse cell");
    check_preserved("missing-cell get_cell read");
    check(!sheet.try_cell("XFD1048576").has_value(),
        "try_cell read should still accept the last legal Excel cell");
    check_preserved("last-legal missing try_cell read");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "valid get_cell read should preserve source-backed cells after read failures");
    check_preserved("valid source-backed get_cell read");

    const std::vector<fastxlsx::WorksheetCellSnapshot> snapshot_after = sheet.sparse_cells();
    check(snapshot_after.size() == snapshot_before.size(),
        "invalid/missing read failures should preserve sparse snapshot size");
    check(snapshot_after[0].reference.row == snapshot_before[0].reference.row &&
            snapshot_after[0].reference.column == snapshot_before[0].reference.column &&
            snapshot_after[0].value.text_value() == snapshot_before[0].value.text_value(),
        "invalid/missing read failures should preserve the source-backed A1 snapshot");
    check(editor.last_edit_error() == prior_error,
        "final sparse snapshot after invalid reads should preserve prior diagnostic");

    editor.save_as(output);
    check(editor.last_edit_error() == prior_error,
        "no-op save_as after invalid cell reads should preserve the prior diagnostic");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as after invalid cell reads should copy source entries");
}

void test_public_worksheet_editor_sparse_cells_snapshot()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-sparse-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-cells-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("snapshot-new"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);

    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells();
    check(cells.size() == sheet.cell_count(),
        "sparse_cells should return one snapshot per active sparse record");
    check(cells.size() == 4,
        "sparse_cells should include source-backed, edited, and explicit blank records");

    check(cells[0].reference.row == 1 && cells[0].reference.column == 1,
        "sparse_cells should be row-major by row then column");
    check(cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[0].value.text_value() == "placeholder-a1",
        "sparse_cells should copy source-backed text values");
    check(cells[1].reference.row == 1 && cells[1].reference.column == 2,
        "sparse_cells should keep same-row cells ordered by column");
    check(cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
            cells[1].value.number_value() == 1.0,
        "sparse_cells should copy source-backed numeric values");
    check(cells[2].reference.row == 3 && cells[2].reference.column == 2 &&
            cells[2].value.kind() == fastxlsx::CellValueKind::Blank,
        "sparse_cells should include explicit blank records");
    check(cells[3].reference.row == 4 && cells[3].reference.column == 4 &&
            cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[3].value.text_value() == "snapshot-new",
        "sparse_cells should include edited cells");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("changed-after-snapshot"));
    check(cells[0].value.text_value() == "placeholder-a1",
        "sparse_cells should return owning snapshots, not borrowed store references");
    check(!editor.last_edit_error().has_value(),
        "sparse_cells read should not update last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "sparse_cells should not interfere with dirty-session save_as");
    check_contains(worksheet_xml, "changed-after-snapshot",
        "subsequent edits after sparse_cells should still persist");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "sparse_cells should not revive erased source cells");
}

void test_public_worksheet_editor_sparse_cells_range_snapshot()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-sparse-range-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-range-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("range-excluded"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("range-new"));
    sheet.erase_cell(2, 1);

    const fastxlsx::CellRange range {1, 2, 3, 3};
    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells(range);
    check(cells.size() == 3,
        "range sparse_cells should return only active sparse records inside the range");
    check(cells[0].reference.row == 1 && cells[0].reference.column == 2 &&
            cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
            cells[0].value.number_value() == 1.0,
        "range sparse_cells should include source-backed cells in row-major order");
    check(cells[1].reference.row == 3 && cells[1].reference.column == 2 &&
            cells[1].value.kind() == fastxlsx::CellValueKind::Blank,
        "range sparse_cells should include explicit blank records");
    check(cells[2].reference.row == 3 && cells[2].reference.column == 3 &&
            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[2].value.text_value() == "range-new",
        "range sparse_cells should include edited records in range");

    const std::vector<fastxlsx::WorksheetCellSnapshot> empty_range =
        sheet.sparse_cells(fastxlsx::CellRange {2, 2, 2, 3});
    check(empty_range.empty(),
        "range sparse_cells should not synthesize missing cells as blank snapshots");

    sheet.set_cell(1, 2, fastxlsx::CellValue::number(2.0));
    check(cells[0].value.number_value() == 1.0,
        "range sparse_cells should return owning snapshots, not borrowed records");
    check(!editor.last_edit_error().has_value(),
        "range sparse_cells read should not update last_edit_error");

    const std::size_t cell_count_before_invalid_ranges = sheet.cell_count();
    const std::array<fastxlsx::CellRange, 4> invalid_ranges {
        fastxlsx::CellRange {3, 3, 1, 1},
        fastxlsx::CellRange {0, 1, 1, 1},
        fastxlsx::CellRange {1, 1, 1048577, 1},
        fastxlsx::CellRange {1, 1, 1, 16385},
    };
    for (const fastxlsx::CellRange invalid_range : invalid_ranges) {
        check(threw_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid_range); }),
            "range sparse_cells should reject invalid CellRange values");
    }
    check(sheet.cell_count() == cell_count_before_invalid_ranges,
        "invalid range sparse_cells calls should not mutate sparse store state");
    check(!editor.last_edit_error().has_value(),
        "invalid range sparse_cells calls should not update last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "range sparse_cells should not interfere with dirty-session save_as");
    check_contains(worksheet_xml, "range-excluded",
        "cells outside the inspected range should still persist");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "range sparse_cells should not revive erased source cells");
}

void test_public_worksheet_editor_sparse_cells_a1_range_snapshot()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-sparse-a1-range-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-a1-range-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("a1-range-excluded"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("a1-range-new"));
    sheet.erase_cell(2, 1);

    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells("B1:C3");
    check(cells.size() == 3,
        "A1 range sparse_cells should return only active sparse records inside the range");
    check(cells[0].reference.row == 1 && cells[0].reference.column == 2 &&
            cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
            cells[0].value.number_value() == 1.0,
        "A1 range sparse_cells should include source-backed cells in row-major order");
    check(cells[1].reference.row == 3 && cells[1].reference.column == 2 &&
            cells[1].value.kind() == fastxlsx::CellValueKind::Blank,
        "A1 range sparse_cells should include explicit blank records");
    check(cells[2].reference.row == 3 && cells[2].reference.column == 3 &&
            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[2].value.text_value() == "a1-range-new",
        "A1 range sparse_cells should include edited records in range");

    const std::vector<fastxlsx::WorksheetCellSnapshot> single_cell = sheet.sparse_cells("B1");
    check(single_cell.size() == 1 && single_cell[0].reference.row == 1 &&
            single_cell[0].reference.column == 2 &&
            single_cell[0].value.kind() == fastxlsx::CellValueKind::Number &&
            single_cell[0].value.number_value() == 1.0,
        "A1 range sparse_cells should accept a single cell as a one-cell range");

    const std::vector<fastxlsx::WorksheetCellSnapshot> empty_range =
        sheet.sparse_cells("B2:C2");
    check(empty_range.empty(),
        "A1 range sparse_cells should not synthesize missing cells as blank snapshots");

    sheet.set_cell(1, 2, fastxlsx::CellValue::number(2.0));
    check(cells[0].value.number_value() == 1.0,
        "A1 range sparse_cells should return owning snapshots, not borrowed records");
    check(!editor.last_edit_error().has_value(),
        "A1 range sparse_cells read should not update last_edit_error");

    const std::size_t cell_count_before_invalid_ranges = sheet.cell_count();
    const std::array<std::string_view, 13> invalid_ranges {
        "",
        "a1:B2",
        "A1:b2",
        "A0:B2",
        "A01:B2",
        "XFE1:XFE2",
        "A1:A1048577",
        "A1:B2:C3",
        "B2:A1",
        "A:C",
        "1:3",
        "$A$1:$B$2",
        "Data!A1:B2",
    };
    for (const std::string_view invalid_range : invalid_ranges) {
        check(threw_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid_range); }),
            "A1 range sparse_cells should reject invalid range references");
    }
    check(sheet.cell_count() == cell_count_before_invalid_ranges,
        "invalid A1 range sparse_cells calls should not mutate sparse store state");
    check(!editor.last_edit_error().has_value(),
        "invalid A1 range sparse_cells calls should not update last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "A1 range sparse_cells should not interfere with dirty-session save_as");
    check_contains(worksheet_xml, "a1-range-excluded",
        "cells outside the inspected A1 range should still persist");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "A1 range sparse_cells should not revive erased source cells");
}

void test_public_worksheet_editor_sparse_cells_coordinate_batch_snapshot()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-sparse-batch-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-batch-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("batch-new"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);

    const std::array<fastxlsx::WorksheetCellReference, 6> batch {
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {3, 2},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 1},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> batch_snapshots = sheet.sparse_cells(batch);
    check(batch_snapshots.size() == 5,
        "sparse_cells(span<WorksheetCellReference>) should skip missing cells and keep duplicates");
    check(batch_snapshots[0].reference.row == 4 && batch_snapshots[0].reference.column == 4 &&
            batch_snapshots[0].value.kind() == fastxlsx::CellValueKind::Text &&
            batch_snapshots[0].value.text_value() == "batch-new",
        "sparse_cells(span<WorksheetCellReference>) should keep the input order");
    check(batch_snapshots[1].reference.row == 1 && batch_snapshots[1].reference.column == 2 &&
            batch_snapshots[1].value.kind() == fastxlsx::CellValueKind::Number &&
            batch_snapshots[1].value.number_value() == 1.0,
        "sparse_cells(span<WorksheetCellReference>) should include source-backed cells");
    check(batch_snapshots[2].reference.row == 3 && batch_snapshots[2].reference.column == 2 &&
            batch_snapshots[2].value.kind() == fastxlsx::CellValueKind::Blank,
        "sparse_cells(span<WorksheetCellReference>) should include explicit blank records");
    check(batch_snapshots[3].reference.row == 1 && batch_snapshots[3].reference.column == 1 &&
            batch_snapshots[3].value.kind() == fastxlsx::CellValueKind::Text &&
            batch_snapshots[3].value.text_value() == "placeholder-a1",
        "sparse_cells(span<WorksheetCellReference>) should include active records after skipping missing coordinates");
    check(batch_snapshots[4].reference.row == 1 && batch_snapshots[4].reference.column == 1 &&
            batch_snapshots[4].value.kind() == fastxlsx::CellValueKind::Text &&
            batch_snapshots[4].value.text_value() == "placeholder-a1",
        "sparse_cells(span<WorksheetCellReference>) should preserve duplicate coordinates");

    const std::vector<fastxlsx::WorksheetCellSnapshot> initializer_batch = sheet.sparse_cells({
        {4, 4},
        {1, 1},
    });
    check(initializer_batch.size() == 2,
        "sparse_cells(initializer_list<WorksheetCellReference>) should return the requested records");
    check(initializer_batch[0].reference.row == 4 && initializer_batch[0].reference.column == 4 &&
            initializer_batch[0].value.text_value() == "batch-new",
        "sparse_cells(initializer_list<WorksheetCellReference>) should preserve input order");
    check(initializer_batch[1].reference.row == 1 && initializer_batch[1].reference.column == 1 &&
            initializer_batch[1].value.text_value() == "placeholder-a1",
        "sparse_cells(initializer_list<WorksheetCellReference>) should return owning snapshots");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("changed-after-batch-snapshot"));
    check(batch_snapshots[3].value.text_value() == "placeholder-a1",
        "sparse_cells(span<WorksheetCellReference>) should return owning snapshots");
    check(initializer_batch[1].value.text_value() == "placeholder-a1",
        "sparse_cells(initializer_list<WorksheetCellReference>) should also return owning snapshots");
    check(!editor.last_edit_error().has_value(),
        "sparse_cells(batch) reads should not update last_edit_error");

    const std::size_t cell_count_before_invalid_reads = sheet.cell_count();
    const std::size_t memory_before_invalid_reads = sheet.estimated_memory_usage();
    const std::array<fastxlsx::WorksheetCellReference, 4> invalid_batch {
        fastxlsx::WorksheetCellReference {0, 1},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 16385},
        fastxlsx::WorksheetCellReference {1048577, 1},
    };
    check(threw_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid_batch); }),
        "sparse_cells(span<WorksheetCellReference>) should reject invalid coordinates");
    check(!editor.last_edit_error().has_value(),
        "invalid sparse_cells(batch) calls should not update last_edit_error");
    check(sheet.cell_count() == cell_count_before_invalid_reads,
        "invalid sparse_cells(batch) calls should not mutate sparse store state");
    check(sheet.estimated_memory_usage() == memory_before_invalid_reads,
        "invalid sparse_cells(batch) calls should not change sparse-store memory usage");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "changed-after-batch-snapshot",
        "sparse_cells(batch) reads should not interfere with later edits");
    check_contains(worksheet_xml, "batch-new",
        "sparse_cells(batch) reads should not interfere with dirty save_as output");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "sparse_cells(batch) reads should not revive erased source cells");
}

void test_public_worksheet_editor_used_range_tracks_sparse_bounds()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-used-range-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-used-range-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 2,
        "used_range should cover source-backed sparse records");
    check(!sheet.has_pending_changes(),
        "used_range should not dirty a freshly materialized worksheet");
    check(!editor.last_edit_error().has_value(),
        "used_range should not update last_edit_error");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("used-range-new"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);
    check_cell_range_equals(sheet.used_range(), 1, 1, 4, 4,
        "used_range should expand to include edited cells and explicit blanks");

    sheet.erase_cell(4, 4);
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 2,
        "used_range should shrink when edge sparse records are erased");
    sheet.erase_cell(3, 2);
    check_cell_range_equals(sheet.used_range(), 1, 1, 1, 2,
        "used_range should shrink back to remaining source-backed records");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-used-range-sentinel"));
    }), "invalid mutation should seed last_edit_error before used_range inspection");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "invalid mutation should populate last_edit_error before used_range inspection");
    check_cell_range_equals(sheet.used_range(), 1, 1, 1, 2,
        "used_range should remain available after a failed mutation");
    check(editor.last_edit_error() == prior_error,
        "used_range should preserve prior last_edit_error diagnostics");

    sheet.erase_cells(fastxlsx::CellRange {1, 1, 1, 2});
    check(!editor.last_edit_error().has_value(),
        "successful erase_cells should clear prior public edit diagnostics");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-empty-used-range"));
    }), "invalid mutation should seed last_edit_error before empty used_range inspection");
    const std::optional<std::string> empty_prior_error = editor.last_edit_error();
    check(empty_prior_error.has_value(),
        "invalid mutation should populate last_edit_error before empty used_range inspection");
    check(!sheet.used_range().has_value(),
        "used_range should return nullopt for an empty materialized sparse store");
    check(editor.last_edit_error() == empty_prior_error,
        "used_range empty-store inspection should preserve prior diagnostics");

    editor.save_as(output);
    check(editor.last_edit_error() == empty_prior_error,
        "save_as after used_range inspection should preserve prior diagnostics");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1"/>)",
        "used_range should not interfere with dirty-session save_as projection");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "used_range should not revive erased A1 text");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "used_range should not revive erased B1 numeric cell");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "used_range should preserve untouched worksheets");
}

void test_public_worksheet_editor_contains_cell_tracks_represented_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-contains-cell-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(sheet.contains_cell(1, 1),
        "contains_cell(row, column) should report source-backed sparse records");
    check(sheet.contains_cell("B1"),
        "contains_cell(A1) should report source-backed sparse records");
    check(!sheet.contains_cell(5, 5),
        "contains_cell should return false for missing sparse records");
    check(!sheet.contains_cell("XFD1048576"),
        "contains_cell(A1) should accept the last legal coordinate as a miss");
    check(!sheet.has_pending_changes(),
        "contains_cell should not dirty a freshly materialized worksheet");
    check(!editor.last_edit_error().has_value(),
        "contains_cell should not update last_edit_error");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("contains-new"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);
    check(sheet.contains_cell(4, 4),
        "contains_cell should see newly inserted sparse records");
    check(sheet.contains_cell("B3"),
        "contains_cell should treat explicit blanks as represented cells");
    check(!sheet.contains_cell(2, 1),
        "contains_cell should return false after erase_cell removes a sparse record");

    const std::optional<fastxlsx::CellValue> copied_blank = sheet.try_cell("B3");
    check(copied_blank.has_value() &&
            copied_blank->kind() == fastxlsx::CellValueKind::Blank,
        "contains_cell should agree with try_cell for represented explicit blanks");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-contains-cell-sentinel"));
    }), "invalid mutation should seed last_edit_error before contains_cell inspection");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "invalid mutation should populate last_edit_error before contains_cell inspection");
    check(sheet.contains_cell("D4"),
        "contains_cell should remain available after a failed mutation");
    check(!sheet.contains_cell(2, 1),
        "contains_cell should keep erased cells missing after a failed mutation");
    check(editor.last_edit_error() == prior_error,
        "contains_cell should preserve prior last_edit_error diagnostics");

    const std::size_t cell_count_before_invalid_reads = sheet.cell_count();
    const std::size_t memory_before_invalid_reads = sheet.estimated_memory_usage();
    check(threw_fastxlsx_error([&] { (void)sheet.contains_cell(0, 1); }),
        "contains_cell(row, column) should reject row zero");
    check(threw_fastxlsx_error([&] { (void)sheet.contains_cell(1, 16385); }),
        "contains_cell(row, column) should reject columns past XFD");
    check(threw_fastxlsx_error([&] { (void)sheet.contains_cell("a1"); }),
        "contains_cell(A1) should reject lowercase references");
    check(threw_fastxlsx_error([&] { (void)sheet.contains_cell("A1:B2"); }),
        "contains_cell(A1) should reject ranges");
    check(editor.last_edit_error() == prior_error,
        "invalid contains_cell reads should not replace last_edit_error diagnostics");
    check(sheet.cell_count() == cell_count_before_invalid_reads,
        "invalid contains_cell reads should not mutate sparse store state");
    check(sheet.estimated_memory_usage() == memory_before_invalid_reads,
        "invalid contains_cell reads should not change sparse-store memory usage");
}

void test_public_worksheet_editor_row_and_column_cells_snapshot()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-row-column-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-row-column-cells-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(1, 3, fastxlsx::CellValue::blank());
    sheet.set_cell(3, 1, fastxlsx::CellValue::text("column-new"));
    sheet.set_cell(4, 4, fastxlsx::CellValue::text("outside-row-column"));

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 3,
        "row_cells should return only active sparse records in the requested row");
    check(row_one[0].reference.row == 1 && row_one[0].reference.column == 1 &&
            row_one[0].value.text_value() == "placeholder-a1",
        "row_cells should include source-backed row cells in column order");
    check(row_one[1].reference.row == 1 && row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        "row_cells should keep same-row cells ordered by column");
    check(row_one[2].reference.row == 1 && row_one[2].reference.column == 3 &&
            row_one[2].value.kind() == fastxlsx::CellValueKind::Blank,
        "row_cells should include explicit blank records");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two = sheet.row_cells(2);
    check(row_two.size() == 1 && row_two[0].reference.row == 2 &&
            row_two[0].reference.column == 1 &&
            row_two[0].value.text_value() == "placeholder-a2",
        "row_cells should return represented records from other source-backed rows");
    check(sheet.row_cells(99).empty(),
        "row_cells should not synthesize missing rows or missing cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one = sheet.column_cells(1);
    check(column_one.size() == 3,
        "column_cells should return only active sparse records in the requested column");
    check(column_one[0].reference.row == 1 && column_one[0].reference.column == 1 &&
            column_one[0].value.text_value() == "placeholder-a1",
        "column_cells should include source-backed records in row order");
    check(column_one[1].reference.row == 2 && column_one[1].reference.column == 1 &&
            column_one[1].value.text_value() == "placeholder-a2",
        "column_cells should keep column records ordered by row");
    check(column_one[2].reference.row == 3 && column_one[2].reference.column == 1 &&
            column_one[2].value.text_value() == "column-new",
        "column_cells should include edited column records");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_two = sheet.column_cells(2);
    check(column_two.size() == 1 && column_two[0].reference.row == 1 &&
            column_two[0].reference.column == 2 &&
            column_two[0].value.number_value() == 1.0,
        "column_cells should return represented records from other columns");
    check(sheet.column_cells(99).empty(),
        "column_cells should not synthesize missing columns or missing cells");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("changed-after-row-column-snapshot"));
    check(row_one[0].value.text_value() == "placeholder-a1",
        "row_cells should return owning snapshots, not borrowed store references");
    check(column_one[0].value.text_value() == "placeholder-a1",
        "column_cells should return owning snapshots, not borrowed store references");
    check(!editor.last_edit_error().has_value(),
        "row_cells and column_cells reads should not update last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "row_cells and column_cells should not interfere with dirty-session save_as");
    check_contains(worksheet_xml, "changed-after-row-column-snapshot",
        "edits after row_cells and column_cells snapshots should still persist");
    check_contains(worksheet_xml, "outside-row-column",
        "cells outside inspected rows and columns should still persist");
}

void test_public_worksheet_editor_row_and_column_cells_invalid_reads_preserve_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-row-column-cells-invalid-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-row-column-cells-invalid-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before = sheet.cell_count();
    const std::size_t memory_before = sheet.estimated_memory_usage();

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-row-column-read-sentinel"));
    }), "invalid coordinate mutation should seed last_edit_error before row/column reads");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "invalid coordinate mutation should record a prior diagnostic");

    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(0); }),
        "row_cells should reject zero row reads");
    check(editor.last_edit_error() == prior_error,
        "invalid row_cells read should preserve prior last_edit_error");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(1048577); }),
        "row_cells should reject row overflow reads");
    check(editor.last_edit_error() == prior_error,
        "overflow row_cells read should preserve prior last_edit_error");
    check(threw_fastxlsx_error([&] { (void)sheet.column_cells(0); }),
        "column_cells should reject zero column reads");
    check(editor.last_edit_error() == prior_error,
        "invalid column_cells read should preserve prior last_edit_error");
    check(threw_fastxlsx_error([&] { (void)sheet.column_cells(16385); }),
        "column_cells should reject column overflow reads");
    check(editor.last_edit_error() == prior_error,
        "overflow column_cells read should preserve prior last_edit_error");

    check(sheet.row_cells(10).empty(),
        "valid missing row_cells reads should return an empty snapshot");
    check(sheet.column_cells(10).empty(),
        "valid missing column_cells reads should return an empty snapshot");
    check(editor.last_edit_error() == prior_error,
        "valid row_cells and column_cells reads should also preserve prior diagnostics");

    check(!sheet.has_pending_changes(),
        "row_cells and column_cells read failures should not dirty the materialized sheet");
    check(!editor.has_pending_changes(),
        "row_cells and column_cells read failures should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "row_cells and column_cells read failures should not increment public edit count");
    check(sheet.cell_count() == cell_count_before,
        "row_cells and column_cells read failures should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == memory_before,
        "row_cells and column_cells read failures should preserve sparse memory estimate");

    editor.save_as(output);
    check(editor.last_edit_error() == prior_error,
        "no-op save_as after row/column read failures should preserve the prior diagnostic");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as after row/column read failures should copy source entries");
}

void test_public_worksheet_editor_sparse_cells_invalid_range_preserves_prior_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-sparse-range-error-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-range-error-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const auto check_source_snapshot =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells, std::string_view prefix) {
            check(cells.size() == 3, std::string(prefix) + " should expose the three source cells");
            check(cells[0].reference.row == 1 && cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[0].value.text_value() == "placeholder-a1",
                std::string(prefix) + " should keep source A1 text");
            check(cells[1].reference.row == 1 && cells[1].reference.column == 2 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    cells[1].value.number_value() == 1.0,
                std::string(prefix) + " should keep source B1 number");
            check(cells[2].reference.row == 2 && cells[2].reference.column == 1 &&
                    cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[2].value.text_value() == "placeholder-a2",
                std::string(prefix) + " should keep source A2 text");
        };

    const std::vector<fastxlsx::WorksheetCellSnapshot> cells_before = sheet.sparse_cells();
    const std::size_t cell_count_before = sheet.cell_count();
    const std::size_t memory_before = sheet.estimated_memory_usage();
    check_source_snapshot(cells_before, "initial sparse snapshot");
    check(!sheet.has_pending_changes(),
        "clean sparse range diagnostic test should start with a clean sheet");
    check(!editor.has_pending_changes(),
        "clean sparse range diagnostic test should start with a clean editor");
    check(editor.pending_change_count() == 0,
        "clean sparse range diagnostic test should start with no public edits");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-range-sentinel"));
    }), "invalid coordinate mutation should seed last_edit_error before range read failure");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "invalid coordinate mutation should record a prior diagnostic");
    if (prior_error.has_value()) {
        check_contains(*prior_error, "WorksheetEditor cell coordinate is invalid",
            "prior diagnostic should be the seeded invalid coordinate failure");
    }

    check(!sheet.has_pending_changes(),
        "seeded invalid coordinate mutation should not dirty the materialized sheet");
    check(!editor.has_pending_changes(),
        "seeded invalid coordinate mutation should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "seeded invalid coordinate mutation should not increment public edit count");
    check(sheet.cell_count() == cell_count_before,
        "seeded invalid coordinate mutation should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == memory_before,
        "seeded invalid coordinate mutation should preserve sparse memory estimate");

    const std::array<fastxlsx::CellRange, 4> invalid_ranges {
        fastxlsx::CellRange {2, 1, 1, 2},
        fastxlsx::CellRange {0, 1, 1, 1},
        fastxlsx::CellRange {1, 1, 1048577, 1},
        fastxlsx::CellRange {1, 1, 1, 16385},
    };
    for (const fastxlsx::CellRange invalid_range : invalid_ranges) {
        check(threw_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid_range); }),
            "invalid range sparse_cells should throw before mutating public diagnostics");
        check(editor.last_edit_error() == prior_error,
            "invalid range sparse_cells should preserve the prior last_edit_error");
    }
    const std::array<std::string_view, 6> invalid_range_references {
        "a1:B2",
        "A1:B2:C3",
        "B2:A1",
        "A:C",
        "$A$1:$B$2",
        "Data!A1:B2",
    };
    for (const std::string_view invalid_range_reference : invalid_range_references) {
        check(threw_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid_range_reference); }),
            "invalid A1 range sparse_cells should throw before mutating public diagnostics");
        check(editor.last_edit_error() == prior_error,
            "invalid A1 range sparse_cells should preserve the prior last_edit_error");
    }

    check(!sheet.has_pending_changes(),
        "invalid range sparse_cells should not dirty the materialized sheet");
    check(!editor.has_pending_changes(),
        "invalid range sparse_cells should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "invalid range sparse_cells should not increment public edit count");
    check(editor.pending_worksheet_edits().empty(),
        "invalid range sparse_cells should not expose pending worksheet summaries");
    check(sheet.cell_count() == cell_count_before,
        "invalid range sparse_cells should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == memory_before,
        "invalid range sparse_cells should preserve sparse memory estimate");

    const std::vector<fastxlsx::WorksheetCellSnapshot> cells_after = sheet.sparse_cells();
    check(editor.last_edit_error() == prior_error,
        "valid sparse_cells inspection after invalid ranges should still preserve prior diagnostic");
    check_source_snapshot(cells_after, "post-invalid-range sparse snapshot");

    editor.save_as(output);
    check(editor.last_edit_error() == prior_error,
        "no-op save_as after invalid range reads should preserve the prior diagnostic");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as after invalid range reads should copy source entries");
}

void test_public_worksheet_editor_erase_cell_auto_flushes_on_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-erase-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(sheet.cell_count() == 3,
        "public WorksheetEditor should materialize the supported source cells");
    sheet.erase_cell(2, 1);
    check(!sheet.try_cell(2, 1).has_value(),
        "public WorksheetEditor erase_cell should remove the sparse record");
    check(sheet.cell_count() == 2,
        "public WorksheetEditor erase_cell should update sparse cell count");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "public WorksheetEditor erase_cell should persist through save_as");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "public WorksheetEditor erase_cell should shrink the projected dimension");
}

void test_public_worksheet_editor_erase_cells_range_reacquires_saved_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-range-erase-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-range-erase-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-range-erase-second.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(sheet.cell_count() == 3,
        "public WorksheetEditor should materialize all represented source cells before range erase");
    sheet.erase_cells(fastxlsx::CellRange {1, 1, 2, 2});
    check(!sheet.try_cell("A1").has_value(),
        "public WorksheetEditor range erase should remove A1");
    check(!sheet.try_cell("B1").has_value(),
        "public WorksheetEditor range erase should remove B1");
    check(!sheet.try_cell("A2").has_value(),
        "public WorksheetEditor range erase should remove A2");
    check(sheet.cell_count() == 0,
        "public WorksheetEditor range erase should update sparse cell count");
    check(sheet.has_pending_changes(),
        "public WorksheetEditor range erase should dirty the materialized sheet");
    check(editor.has_pending_changes(),
        "public WorksheetEditor range erase should dirty the owning editor");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "successful save_as should clear range-erased materialized sheet dirty state");
    check(editor.pending_change_count() == 1,
        "range erase save_as should expose one materialized worksheet handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1"/>)",
        "range erase of all represented cells should shrink the projected dimension to A1");
    check_not_contains(first_worksheet_xml, "placeholder-a1",
        "range erase save_as should omit erased A1 text");
    check_not_contains(first_worksheet_xml, "placeholder-a2",
        "range erase save_as should omit erased A2 text");
    check_not_contains(first_worksheet_xml, R"(r="B1")",
        "range erase save_as should omit erased B1 numeric cell");
    check_contains(first_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "range erase save_as should preserve untouched worksheets");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes(),
        "matching worksheet reacquire after range erase save_as should be clean");
    check(reacquired.cell_count() == 0,
        "matching worksheet reacquire should reuse the erased sparse state");
    check(!reacquired.try_cell("A1").has_value(),
        "matching worksheet reacquire should keep erased A1 missing");
    check(!reacquired.try_cell("B1").has_value(),
        "matching worksheet reacquire should keep erased B1 missing");
    check(!reacquired.try_cell("A2").has_value(),
        "matching worksheet reacquire should keep erased A2 missing");

    reacquired.erase_cells(fastxlsx::CellRange {1, 1, 2, 2});
    check(!reacquired.has_pending_changes(),
        "missing-only range erase after matching reacquire should remain a clean no-op");
    check(!editor.last_edit_error().has_value(),
        "missing-only range erase after matching reacquire should leave diagnostics clear");

    reacquired.set_cell(3, 3, fastxlsx::CellValue::text("range-erase-reacquired"));
    check(reacquired.has_pending_changes(),
        "post-reacquire mutation should dirty the reused materialized sheet");

    editor.save_as(second_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_worksheet_xml, "range-erase-reacquired",
        "post-reacquire mutation should persist on the second save_as");
    check_not_contains(second_worksheet_xml, "placeholder-a1",
        "erased A1 text should not reappear after post-reacquire mutation");
    check_not_contains(second_worksheet_xml, "placeholder-a2",
        "erased A2 text should not reappear after post-reacquire mutation");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "erased B1 numeric cell should not reappear after post-reacquire mutation");
}

void test_public_worksheet_editor_initializer_list_batch_overloads()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-init-list-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-init-list-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cells({
        {fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::CellValue::text("init-list-full-replace")},
        {fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::CellValue::text("init-list-first")},
        {fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::CellValue::formula("A1+B1")},
    });
    check(sheet.get_cell("A1").text_value() == "init-list-full-replace",
        "initializer-list set_cells should replace existing sparse cells");
    const fastxlsx::CellValue duplicate_full_update = sheet.get_cell("C3");
    check(duplicate_full_update.kind() == fastxlsx::CellValueKind::Formula
            && duplicate_full_update.text_value() == "A1+B1",
        "initializer-list set_cells should keep duplicate-coordinate later-wins semantics");

    sheet.set_cell_values({
        {fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::CellValue::number(42.0)},
        {fastxlsx::WorksheetCellReference {4, 4},
            fastxlsx::CellValue::boolean(true)},
    });
    check(sheet.get_cell("B1").number_value() == 42.0,
        "initializer-list set_cell_values should update existing sparse cells");
    check(sheet.get_cell("D4").boolean_value(),
        "initializer-list set_cell_values should insert missing sparse cells");

    sheet.clear_cell_values({
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {6, 6},
    });
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "initializer-list clear_cell_values should clear represented cells");
    check(!sheet.try_cell("F6").has_value(),
        "initializer-list clear_cell_values should not synthesize missing cells");

    sheet.erase_cells({
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {3, 3},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {8, 8},
    });
    check(!sheet.try_cell("A1").has_value(),
        "initializer-list erase_cells should remove represented cells");
    check(!sheet.try_cell("C3").has_value(),
        "initializer-list erase_cells should remove represented duplicate target");
    check(!sheet.try_cell("H8").has_value(),
        "initializer-list erase_cells should not synthesize missing cells");
    check(!editor.last_edit_error().has_value(),
        "successful initializer-list batch overloads should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="B1"/>)",
        "initializer-list clear_cell_values should persist explicit blank cells");
    check_contains(worksheet_xml, R"(<c r="D4" t="b"><v>1</v></c>)",
        "initializer-list set_cell_values should persist inserted boolean cells");
    check_not_contains(worksheet_xml, "init-list-full-replace",
        "initializer-list erase_cells should omit erased full replacement text");
    check_not_contains(worksheet_xml, "A1+B1",
        "initializer-list erase_cells should omit erased formula text");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "initializer-list batch overloads should preserve untouched worksheets");
}

void test_public_worksheet_editor_append_row_appends_after_sparse_max_row()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-append-row-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-append-row-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.append_row({
        fastxlsx::CellValue::text("appended-a3"),
        fastxlsx::CellValue::number(7.0),
        fastxlsx::CellValue::formula("A3+B1"),
        fastxlsx::CellValue::blank(),
    });

    check(sheet.cell_count() == 7,
        "append_row should add one sparse record for each appended value");
    check(sheet.get_cell("A3").text_value() == "appended-a3",
        "append_row should write the first value to column A of the next sparse row");
    check(sheet.get_cell("B3").number_value() == 7.0,
        "append_row should write numeric values by input order");
    const fastxlsx::CellValue formula = sheet.get_cell("C3");
    check(formula.kind() == fastxlsx::CellValueKind::Formula
            && formula.text_value() == "A3+B1",
        "append_row should preserve formula text as a formula cell");
    check(sheet.get_cell("D3").kind() == fastxlsx::CellValueKind::Blank,
        "append_row should represent explicit blank values");
    check(sheet.has_pending_changes(),
        "append_row should dirty the materialized worksheet when values are appended");
    check(editor.pending_materialized_cell_count() == 7,
        "append_row should contribute appended sparse records to aggregate diagnostics");
    check(!editor.last_edit_error().has_value(),
        "successful append_row should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "append_row should extend the dirty worksheet dimension to the appended row");
    check_contains(worksheet_xml,
        R"(<row r="3"><c r="A3" t="inlineStr"><is><t>appended-a3</t></is></c>)",
        "append_row should persist text in the appended sparse row");
    check_contains(worksheet_xml, R"(<c r="B3"><v>7</v></c>)",
        "append_row should persist numeric cells in input order");
    check_contains(worksheet_xml, R"(<c r="C3"><f>A3+B1</f></c>)",
        "append_row should persist formula cells in input order");
    check_contains(worksheet_xml, R"(<c r="D3"/>)",
        "append_row should persist explicit blank cells in input order");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "append_row should preserve untouched worksheets");
}

void test_public_worksheet_editor_append_row_noop_and_guardrails()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-append-row-guards-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before append_row empty no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before append_row empty no-op");

        sheet.append_row({});
        check(!editor.last_edit_error().has_value(),
            "empty append_row should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty append_row should not dirty a clean materialized worksheet");
        check(!editor.has_pending_changes(),
            "empty append_row should not make the editor dirty");
        check(sheet.cell_count() == 3,
            "empty append_row should not create sparse row metadata");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        std::vector<fastxlsx::CellValue> too_wide(
            16385, fastxlsx::CellValue::number(1.0));

        bool failed = false;
        try {
            sheet.append_row(too_wide);
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "16384",
                "append_row width failure should expose the Excel column limit");
        }
        check(failed, "append_row should reject more than 16384 values");
        check(!sheet.has_pending_changes(),
            "width failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "width failure should preserve sparse cell count");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        sheet.set_cell("XFD1048576", fastxlsx::CellValue::text("edge-row"));
        const std::size_t dirty_count = sheet.cell_count();

        bool failed = false;
        try {
            sheet.append_row({fastxlsx::CellValue::text("past-edge")});
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "1048576",
                "append_row row-limit failure should expose the Excel row limit");
        }
        check(failed, "append_row should reject appending past the Excel row limit");
        check(sheet.cell_count() == dirty_count,
            "row-limit failure should preserve the previously dirty sparse store");
        check(sheet.get_cell("XFD1048576").text_value() == "edge-row",
            "row-limit failure should preserve the existing edge-row edit");
        check(!sheet.try_cell("A3").has_value(),
            "row-limit failure should not leak rejected appended values");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-append-row-guards-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool failed = false;
        try {
            sheet.append_row({fastxlsx::CellValue::text("max-cells-rejected")});
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "append_row should expose CellStore max_cells guardrail diagnostics");
        }
        check(failed, "append_row should enforce max_cells on new sparse records");
        check(editor.last_edit_error().has_value(),
            "failed append_row max_cells mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "failed append_row max_cells mutation should not dirty the session");
        check(sheet.cell_count() == 3,
            "failed append_row max_cells mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed append_row max_cells mutation should preserve sparse memory estimate");
        check(!sheet.try_cell("A3").has_value(),
            "failed append_row max_cells mutation should not leave rejected cells readable");

        sheet.erase_cell("A2");
        sheet.append_row({fastxlsx::CellValue::text("append-after-erase")});
        check(!editor.last_edit_error().has_value(),
            "successful append_row after budget release should clear last_edit_error");
        check(sheet.get_cell("A2").text_value() == "append-after-erase",
            "append_row should use the current maximum represented row after erase");
        check(sheet.cell_count() == 3,
            "append_row after erase should stay within the exact max_cells budget");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "append-after-erase",
            "append_row after guardrail recovery should persist through save_as");
        check_not_contains(worksheet_xml, "max-cells-rejected",
            "rejected append_row value should not leak into saved output");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "append_row after erase should not resurrect erased source cells");
    }
}

void test_public_worksheet_editor_set_row_replaces_sparse_row()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-row-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-row-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_row(1, {
        fastxlsx::CellValue::text("set-row-a1"),
        fastxlsx::CellValue::number(22.0),
        fastxlsx::CellValue::formula("A1+B2"),
        fastxlsx::CellValue::blank(),
    });

    check(sheet.cell_count() == 5,
        "set_row should replace the target sparse row without clearing other rows");
    check(sheet.get_cell("A1").text_value() == "set-row-a1",
        "set_row should write the first value to column A of the target row");
    check(sheet.get_cell("B1").number_value() == 22.0,
        "set_row should write numeric values by input order");
    const fastxlsx::CellValue formula = sheet.get_cell("C1");
    check(formula.kind() == fastxlsx::CellValueKind::Formula
            && formula.text_value() == "A1+B2",
        "set_row should preserve formula text as a formula cell");
    check(sheet.get_cell("D1").kind() == fastxlsx::CellValueKind::Blank,
        "set_row should represent explicit blank values");
    check(sheet.get_cell("A2").text_value() == "placeholder-a2",
        "set_row should preserve represented cells outside the target row");
    check(!sheet.try_cell("B2").has_value(),
        "set_row should not synthesize cells outside the target row");
    check(sheet.has_pending_changes(),
        "set_row should dirty the materialized worksheet when values are replaced");
    check(editor.pending_materialized_cell_count() == 5,
        "set_row should contribute the replaced sparse records to aggregate diagnostics");
    check(!editor.last_edit_error().has_value(),
        "successful set_row should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "set_row should refresh the dirty worksheet dimension");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" t="inlineStr"><is><t>set-row-a1</t></is></c>)",
        "set_row should persist target-row text in column order");
    check_contains(worksheet_xml, R"(<c r="B1"><v>22</v></c>)",
        "set_row should persist target-row numeric cells");
    check_contains(worksheet_xml, R"(<c r="C1"><f>A1+B2</f></c>)",
        "set_row should persist target-row formula cells");
    check_contains(worksheet_xml, R"(<c r="D1"/>)",
        "set_row should persist explicit blank cells");
    check_contains(worksheet_xml, "placeholder-a2",
        "set_row should keep non-target row source cells");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "set_row should omit the old target-row text");
    check_not_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "set_row should omit the old target-row numeric value");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "set_row should preserve untouched worksheets");
}

void test_public_worksheet_editor_set_row_empty_and_guardrails()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-row-guards-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before set_row empty no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before set_row empty no-op");

        const std::vector<fastxlsx::CellValue> empty_row;
        sheet.set_row(3, empty_row);
        check(!editor.last_edit_error().has_value(),
            "empty set_row on a missing row should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty set_row on a missing row should not dirty a clean materialized worksheet");
        check(!editor.has_pending_changes(),
            "empty set_row on a missing row should not make the editor dirty");
        check(sheet.cell_count() == 3,
            "empty set_row on a missing row should not create sparse row metadata");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-clear-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const std::vector<fastxlsx::CellValue> empty_row;

        sheet.set_row(1, empty_row);
        check(sheet.cell_count() == 1,
            "empty set_row on a represented row should clear that sparse row");
        check(!sheet.try_cell("A1").has_value(),
            "empty set_row should remove target-row text cells");
        check(!sheet.try_cell("B1").has_value(),
            "empty set_row should remove target-row numeric cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "empty set_row should preserve non-target rows");
        check(sheet.has_pending_changes(),
            "empty set_row on a represented row should dirty the materialized worksheet");
        check(!editor.last_edit_error().has_value(),
            "successful row clear should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "placeholder-a2",
            "row clear should persist non-target row source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "row clear should omit cleared text cells");
        check_not_contains(worksheet_xml, R"(r="B1")",
            "row clear should omit cleared numeric cells");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        std::vector<fastxlsx::CellValue> too_wide(
            16385, fastxlsx::CellValue::number(1.0));

        bool failed = false;
        try {
            sheet.set_row(1, too_wide);
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "16384",
                "set_row width failure should expose the Excel column limit");
        }
        check(failed, "set_row should reject more than 16384 values");
        check(!sheet.has_pending_changes(),
            "set_row width failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "set_row width failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_row width failure should preserve existing cells");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool failed = false;
        try {
            sheet.set_row(0, {fastxlsx::CellValue::text("invalid-row")});
        } catch (const fastxlsx::FastXlsxError&) {
            failed = true;
        }
        check(failed, "set_row should reject invalid row numbers");
        check(editor.last_edit_error().has_value(),
            "failed set_row invalid-row mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_row invalid-row failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "set_row invalid-row failure should preserve sparse cell count");
    }

    {
        const std::filesystem::path style_artifact =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-style-source.xlsx");
        fastxlsx::StyleId non_default_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_artifact);
            {
                fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
                non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
                styled_sheet.append_row({
                    fastxlsx::CellView::number(1.0).with_style(non_default_style),
                });
            }
            writer.close();
        }

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool failed = false;
        try {
            sheet.set_row(1, {
                fastxlsx::CellValue::text("styled-row").with_style(non_default_style),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "StyleId",
                "set_row style failure should expose the unsupported StyleId boundary");
        }
        check(failed, "set_row should reject caller-supplied non-default StyleId values");
        check(editor.last_edit_error().has_value(),
            "failed set_row style mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_row style failure should not dirty the materialized worksheet");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_row style failure should preserve existing cells");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-guards-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool failed = false;
        try {
            sheet.set_row(3, {fastxlsx::CellValue::text("max-cells-rejected")});
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "set_row should expose CellStore max_cells guardrail diagnostics");
        }
        check(failed, "set_row should enforce max_cells on staged row replacement");
        check(editor.last_edit_error().has_value(),
            "failed set_row max_cells mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "failed set_row max_cells mutation should not dirty the session");
        check(sheet.cell_count() == 3,
            "failed set_row max_cells mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed set_row max_cells mutation should preserve sparse memory estimate");
        check(!sheet.try_cell("A3").has_value(),
            "failed set_row max_cells mutation should not leave rejected cells readable");

        sheet.set_row(1, {fastxlsx::CellValue::text("replace-row-a1")});
        check(!editor.last_edit_error().has_value(),
            "successful in-budget set_row should clear last_edit_error");
        check(sheet.cell_count() == 2,
            "in-budget set_row should release old target-row records before writing new ones");
        check(sheet.get_cell("A1").text_value() == "replace-row-a1",
            "in-budget set_row should replace the target row");
        check(!sheet.try_cell("B1").has_value(),
            "in-budget set_row should remove old target-row tail cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "in-budget set_row should preserve non-target rows");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "replace-row-a1",
            "in-budget set_row should persist through save_as");
        check_not_contains(worksheet_xml, "max-cells-rejected",
            "rejected set_row value should not leak into saved output");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "in-budget set_row should not resurrect replaced cells");
        check_not_contains(worksheet_xml, R"(r="B1")",
            "in-budget set_row should not persist old target-row tail cells");
    }
}

void test_public_worksheet_editor_set_column_replaces_sparse_column()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-column-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-column-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_column(1, {
        fastxlsx::CellValue::text("set-column-a1"),
        fastxlsx::CellValue::number(44.0),
        fastxlsx::CellValue::formula("A3+B1"),
        fastxlsx::CellValue::blank(),
    });

    check(sheet.cell_count() == 5,
        "set_column should replace the target sparse column without clearing other columns");
    check(sheet.get_cell("A1").text_value() == "set-column-a1",
        "set_column should write the first value to row one of the target column");
    check(sheet.get_cell("A2").number_value() == 44.0,
        "set_column should write numeric values by input order");
    const fastxlsx::CellValue formula = sheet.get_cell("A3");
    check(formula.kind() == fastxlsx::CellValueKind::Formula
            && formula.text_value() == "A3+B1",
        "set_column should preserve formula text as a formula cell");
    check(sheet.get_cell("A4").kind() == fastxlsx::CellValueKind::Blank,
        "set_column should represent explicit blank values");
    check(sheet.get_cell("B1").number_value() == 1.0,
        "set_column should preserve represented cells outside the target column");
    check(!sheet.try_cell("B2").has_value(),
        "set_column should not synthesize cells outside the target column");
    check(sheet.has_pending_changes(),
        "set_column should dirty the materialized worksheet when values are replaced");
    check(editor.pending_materialized_cell_count() == 5,
        "set_column should contribute the replaced sparse records to aggregate diagnostics");
    check(!editor.last_edit_error().has_value(),
        "successful set_column should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B4"/>)",
        "set_column should refresh the dirty worksheet dimension");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" t="inlineStr"><is><t>set-column-a1</t></is></c>)",
        "set_column should persist target-column text in row order");
    check_contains(worksheet_xml, R"(<c r="A2"><v>44</v></c>)",
        "set_column should persist target-column numeric cells");
    check_contains(worksheet_xml, R"(<c r="A3"><f>A3+B1</f></c>)",
        "set_column should persist target-column formula cells");
    check_contains(worksheet_xml, R"(<c r="A4"/>)",
        "set_column should persist explicit blank cells");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "set_column should keep non-target column source cells");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "set_column should omit the old target-column row-one text");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "set_column should omit the old target-column row-two text");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "set_column should preserve untouched worksheets");
}

void test_public_worksheet_editor_set_column_empty_and_guardrails()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-column-guards-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before set_column empty no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before set_column empty no-op");

        const std::vector<fastxlsx::CellValue> empty_column;
        sheet.set_column(3, empty_column);
        check(!editor.last_edit_error().has_value(),
            "empty set_column on a missing column should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty set_column on a missing column should not dirty a clean materialized worksheet");
        check(!editor.has_pending_changes(),
            "empty set_column on a missing column should not make the editor dirty");
        check(sheet.cell_count() == 3,
            "empty set_column on a missing column should not create sparse column metadata");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-clear-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const std::vector<fastxlsx::CellValue> empty_column;

        sheet.set_column(1, empty_column);
        check(sheet.cell_count() == 1,
            "empty set_column on a represented column should clear that sparse column");
        check(!sheet.try_cell("A1").has_value(),
            "empty set_column should remove target-column row-one text cells");
        check(!sheet.try_cell("A2").has_value(),
            "empty set_column should remove target-column row-two text cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "empty set_column should preserve non-target columns");
        check(sheet.has_pending_changes(),
            "empty set_column on a represented column should dirty the materialized worksheet");
        check(!editor.last_edit_error().has_value(),
            "successful column clear should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="B1"/>)",
            "column clear should shrink the projected dimension to remaining sparse cells");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "column clear should persist non-target column source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "column clear should omit cleared row-one text cells");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "column clear should omit cleared row-two text cells");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool failed = false;
        try {
            sheet.set_column(0, {fastxlsx::CellValue::text("invalid-column")});
        } catch (const fastxlsx::FastXlsxError&) {
            failed = true;
        }
        check(failed, "set_column should reject invalid column numbers");
        check(editor.last_edit_error().has_value(),
            "failed set_column invalid-column mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_column invalid-column failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "set_column invalid-column failure should preserve sparse cell count");
    }

    {
        const std::filesystem::path style_artifact =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-style-source.xlsx");
        fastxlsx::StyleId non_default_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_artifact);
            {
                fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
                non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
                styled_sheet.append_row({
                    fastxlsx::CellView::number(1.0).with_style(non_default_style),
                });
            }
            writer.close();
        }

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool failed = false;
        try {
            sheet.set_column(1, {
                fastxlsx::CellValue::text("styled-column").with_style(non_default_style),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "StyleId",
                "set_column style failure should expose the unsupported StyleId boundary");
        }
        check(failed, "set_column should reject caller-supplied non-default StyleId values");
        check(editor.last_edit_error().has_value(),
            "failed set_column style mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_column style failure should not dirty the materialized worksheet");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_column style failure should preserve existing cells");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-budget-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool failed = false;
        try {
            sheet.set_column(3, {
                fastxlsx::CellValue::text("max-cells-rejected-c1"),
                fastxlsx::CellValue::text("max-cells-rejected-c2"),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "set_column should expose CellStore max_cells guardrail diagnostics");
        }
        check(failed, "set_column should enforce max_cells on staged column replacement");
        check(editor.last_edit_error().has_value(),
            "failed set_column max_cells mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "failed set_column max_cells mutation should not dirty the session");
        check(sheet.cell_count() == 3,
            "failed set_column max_cells mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed set_column max_cells mutation should preserve sparse memory estimate");
        check(!sheet.try_cell("C1").has_value(),
            "failed set_column max_cells mutation should not leave rejected cells readable");

        sheet.set_column(1, {fastxlsx::CellValue::text("replace-column-a1")});
        check(!editor.last_edit_error().has_value(),
            "successful in-budget set_column should clear last_edit_error");
        check(sheet.cell_count() == 2,
            "in-budget set_column should release old target-column records before writing new ones");
        check(sheet.get_cell("A1").text_value() == "replace-column-a1",
            "in-budget set_column should replace the target column");
        check(!sheet.try_cell("A2").has_value(),
            "in-budget set_column should remove old target-column tail cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "in-budget set_column should preserve non-target columns");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "replace-column-a1",
            "in-budget set_column should persist through save_as");
        check_not_contains(worksheet_xml, "max-cells-rejected",
            "rejected set_column value should not leak into saved output");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "in-budget set_column should not resurrect replaced cells");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "in-budget set_column should not persist old target-column tail cells");
    }
}

void test_public_worksheet_editor_set_row_values_preserves_styles_and_tail()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-row-values-source.xlsx");

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_row_values(1, {
            fastxlsx::CellValue::text("row-value-a1"),
            fastxlsx::CellValue::blank(),
            fastxlsx::CellValue::formula("A1+B1"),
        });

        check(sheet.cell_count() == 4,
            "set_row_values should update the row prefix without removing sparse tail cells");
        check(sheet.get_cell("A1").text_value() == "row-value-a1",
            "set_row_values should write the first value to column A");
        check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
            "set_row_values should represent explicit blank prefix values");
        const fastxlsx::CellValue formula = sheet.get_cell("C1");
        check(formula.kind() == fastxlsx::CellValueKind::Formula
                && formula.text_value() == "A1+B1",
            "set_row_values should write formulas without evaluating them");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_row_values should preserve represented cells outside the written prefix");
        check(sheet.has_pending_changes(),
            "set_row_values should dirty the materialized worksheet when values change");
        check(!editor.last_edit_error().has_value(),
            "successful set_row_values should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
            "set_row_values should refresh the dirty worksheet dimension");
        check_contains(worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>row-value-a1</t></is></c>)",
            "set_row_values should persist row prefix text cells");
        check_contains(worksheet_xml, R"(<c r="B1"/>)",
            "set_row_values should persist explicit blank prefix cells");
        check_contains(worksheet_xml, R"(<c r="C1"><f>A1+B1</f></c>)",
            "set_row_values should persist row prefix formula cells");
        check_contains(worksheet_xml, "placeholder-a2",
            "set_row_values should preserve non-target row source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "set_row_values should omit the overwritten source value");
        check_not_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_row_values should omit overwritten numeric payloads");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "set_row_values should preserve untouched worksheets");
    }

    {
        const std::filesystem::path style_source =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-style-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-style-output.xlsx");
        fastxlsx::StyleId non_default_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_source);
            {
                fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
                non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
                styled_sheet.append_row({
                    fastxlsx::CellView::number(1.0).with_style(non_default_style),
                    fastxlsx::CellView::text("row-tail"),
                });
            }
            writer.close();
        }

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(style_source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");
        sheet.set_row_values(1, {fastxlsx::CellValue::text("styled-row-value")});

        const fastxlsx::CellValue styled_a1 = sheet.get_cell("A1");
        check(styled_a1.kind() == fastxlsx::CellValueKind::Text
                && styled_a1.text_value() == "styled-row-value"
                && styled_a1.has_style()
                && styled_a1.style_id().value() == non_default_style.value(),
            "set_row_values should preserve source style ids on overwritten targets");
        check(sheet.get_cell("B1").text_value() == "row-tail",
            "set_row_values should leave row cells beyond the input prefix untouched");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        const std::string styled_text =
            R"(<c r="A1" s=")" + std::to_string(non_default_style.value())
            + R"(" t="inlineStr"><is><t>styled-row-value</t></is></c>)";
        check_contains(worksheet_xml, styled_text,
            "set_row_values should persist value-only edits with the source style id");
        check_contains(worksheet_xml, "row-tail",
            "set_row_values should persist untouched row tail cells");

        fastxlsx::WorkbookEditor style_reject_editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor reject_sheet = style_reject_editor.worksheet("Data");
        bool failed = false;
        try {
            reject_sheet.set_row_values(1, {
                fastxlsx::CellValue::text("styled-row-rejected")
                    .with_style(non_default_style),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "StyleId",
                "set_row_values style failure should expose the unsupported StyleId boundary");
        }
        check(failed, "set_row_values should reject caller-supplied non-default StyleId values");
        check(style_reject_editor.last_edit_error().has_value(),
            "failed set_row_values style mutation should update last_edit_error");
        check(!reject_sheet.has_pending_changes(),
            "set_row_values style failure should not dirty the materialized worksheet");
        check(reject_sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_row_values style failure should preserve existing cells");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before set_row_values empty no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before set_row_values empty no-op");

        const std::vector<fastxlsx::CellValue> empty_values;
        sheet.set_row_values(3, empty_values);
        check(!editor.last_edit_error().has_value(),
            "empty set_row_values should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty set_row_values should not dirty a clean materialized worksheet");
        check(sheet.cell_count() == 3,
            "empty set_row_values should not create sparse row metadata");

        bool invalid_failed = false;
        try {
            sheet.set_row_values(0, {fastxlsx::CellValue::text("invalid-row")});
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_failed = true;
        }
        check(invalid_failed, "set_row_values should reject invalid row numbers");
        check(editor.last_edit_error().has_value(),
            "failed set_row_values invalid-row mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_row_values invalid-row failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "set_row_values invalid-row failure should preserve sparse cell count");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-budget-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool failed = false;
        try {
            sheet.set_row_values(3, {fastxlsx::CellValue::text("max-cells-rejected")});
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "set_row_values should expose CellStore max_cells guardrail diagnostics");
        }
        check(failed, "set_row_values should enforce max_cells on staged row-prefix writes");
        check(editor.last_edit_error().has_value(),
            "failed set_row_values max_cells mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "failed set_row_values max_cells mutation should not dirty the session");
        check(sheet.cell_count() == 3,
            "failed set_row_values max_cells mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed set_row_values max_cells mutation should preserve sparse memory estimate");
        check(!sheet.try_cell("A3").has_value(),
            "failed set_row_values max_cells mutation should not leave rejected cells readable");

        sheet.set_row_values(1, {fastxlsx::CellValue::text("in-budget-row-value")});
        check(!editor.last_edit_error().has_value(),
            "successful in-budget set_row_values should clear last_edit_error");
        check(sheet.cell_count() == 3,
            "in-budget set_row_values should keep existing sparse tail records");
        check(sheet.get_cell("A1").text_value() == "in-budget-row-value",
            "in-budget set_row_values should update existing prefix cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "in-budget set_row_values should preserve row tail cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "in-budget set_row_values should preserve non-target rows");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "in-budget-row-value",
            "in-budget set_row_values should persist through save_as");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "in-budget set_row_values should persist row tail cells");
        check_not_contains(worksheet_xml, "max-cells-rejected",
            "rejected set_row_values payload should not leak into saved output");
    }
}

void test_public_worksheet_editor_set_column_values_noop_invalid_and_budget()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-column-values-source.xlsx");

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_column_values(1, {
            fastxlsx::CellValue::text("column-value-a1"),
            fastxlsx::CellValue::blank(),
            fastxlsx::CellValue::number(7.0),
        });

        check(sheet.cell_count() == 4,
            "set_column_values should update the column prefix without removing sparse tail cells");
        check(sheet.get_cell("A1").text_value() == "column-value-a1",
            "set_column_values should write the first value to row one");
        check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
            "set_column_values should represent explicit blank prefix values");
        check(sheet.get_cell("A3").number_value() == 7.0,
            "set_column_values should write numeric prefix values");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "set_column_values should preserve represented cells outside the written prefix");
        check(sheet.has_pending_changes(),
            "set_column_values should dirty the materialized worksheet when values change");
        check(!editor.last_edit_error().has_value(),
            "successful set_column_values should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
            "set_column_values should refresh the dirty worksheet dimension");
        check_contains(worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>column-value-a1</t></is></c>)",
            "set_column_values should persist column prefix text cells");
        check_contains(worksheet_xml, R"(<c r="A2"/>)",
            "set_column_values should persist explicit blank prefix cells");
        check_contains(worksheet_xml, R"(<c r="A3"><v>7</v></c>)",
            "set_column_values should persist column prefix numeric cells");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_column_values should keep non-target column source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "set_column_values should omit the overwritten row-one source value");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "set_column_values should omit the overwritten row-two source value");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "set_column_values should preserve untouched worksheets");
    }

    {
        const std::filesystem::path style_source =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-style-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-style-output.xlsx");
        fastxlsx::StyleId non_default_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_source);
            {
                fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
                non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
                styled_sheet.append_row({
                    fastxlsx::CellView::number(1.0).with_style(non_default_style),
                });
                styled_sheet.append_row({fastxlsx::CellView::text("column-tail")});
            }
            writer.close();
        }

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(style_source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");
        sheet.set_column_values(1, {fastxlsx::CellValue::text("styled-column-value")});

        const fastxlsx::CellValue styled_a1 = sheet.get_cell("A1");
        check(styled_a1.kind() == fastxlsx::CellValueKind::Text
                && styled_a1.text_value() == "styled-column-value"
                && styled_a1.has_style()
                && styled_a1.style_id().value() == non_default_style.value(),
            "set_column_values should preserve source style ids on overwritten targets");
        check(sheet.get_cell("A2").text_value() == "column-tail",
            "set_column_values should leave column cells beyond the input prefix untouched");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        const std::string styled_text =
            R"(<c r="A1" s=")" + std::to_string(non_default_style.value())
            + R"(" t="inlineStr"><is><t>styled-column-value</t></is></c>)";
        check_contains(worksheet_xml, styled_text,
            "set_column_values should persist value-only edits with the source style id");
        check_contains(worksheet_xml, "column-tail",
            "set_column_values should persist untouched column tail cells");

        fastxlsx::WorkbookEditor style_reject_editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor reject_sheet = style_reject_editor.worksheet("Data");
        bool failed = false;
        try {
            reject_sheet.set_column_values(1, {
                fastxlsx::CellValue::text("styled-column-rejected")
                    .with_style(non_default_style),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "StyleId",
                "set_column_values style failure should expose the unsupported StyleId boundary");
        }
        check(failed,
            "set_column_values should reject caller-supplied non-default StyleId values");
        check(style_reject_editor.last_edit_error().has_value(),
            "failed set_column_values style mutation should update last_edit_error");
        check(!reject_sheet.has_pending_changes(),
            "set_column_values style failure should not dirty the materialized worksheet");
        check(reject_sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_column_values style failure should preserve existing cells");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before set_column_values empty no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before set_column_values empty no-op");

        const std::vector<fastxlsx::CellValue> empty_values;
        sheet.set_column_values(3, empty_values);
        check(!editor.last_edit_error().has_value(),
            "empty set_column_values should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty set_column_values should not dirty a clean materialized worksheet");
        check(sheet.cell_count() == 3,
            "empty set_column_values should not create sparse column metadata");

        bool invalid_failed = false;
        try {
            sheet.set_column_values(0, {fastxlsx::CellValue::text("invalid-column")});
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_failed = true;
        }
        check(invalid_failed, "set_column_values should reject invalid column numbers");
        check(editor.last_edit_error().has_value(),
            "failed set_column_values invalid-column mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_column_values invalid-column failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "set_column_values invalid-column failure should preserve sparse cell count");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-budget-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool failed = false;
        try {
            sheet.set_column_values(3, {
                fastxlsx::CellValue::text("max-cells-rejected"),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "set_column_values should expose CellStore max_cells guardrail diagnostics");
        }
        check(failed,
            "set_column_values should enforce max_cells on staged column-prefix writes");
        check(editor.last_edit_error().has_value(),
            "failed set_column_values max_cells mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "failed set_column_values max_cells mutation should not dirty the session");
        check(sheet.cell_count() == 3,
            "failed set_column_values max_cells mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed set_column_values max_cells mutation should preserve sparse memory estimate");
        check(!sheet.try_cell("C1").has_value(),
            "failed set_column_values max_cells mutation should not leave rejected cells readable");

        sheet.set_column_values(1, {fastxlsx::CellValue::text("in-budget-column-value")});
        check(!editor.last_edit_error().has_value(),
            "successful in-budget set_column_values should clear last_edit_error");
        check(sheet.cell_count() == 3,
            "in-budget set_column_values should keep existing sparse tail records");
        check(sheet.get_cell("A1").text_value() == "in-budget-column-value",
            "in-budget set_column_values should update existing prefix cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "in-budget set_column_values should preserve column tail cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "in-budget set_column_values should preserve non-target columns");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "in-budget-column-value",
            "in-budget set_column_values should persist through save_as");
        check_contains(worksheet_xml, "placeholder-a2",
            "in-budget set_column_values should persist column tail cells");
        check_not_contains(worksheet_xml, "max-cells-rejected",
            "rejected set_column_values payload should not leak into saved output");
    }
}

void test_public_worksheet_editor_clear_and_erase_all_cells()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-clear-erase-all-source.xlsx");

    {
        const std::filesystem::path style_source =
            artifact("fastxlsx-workbook-editor-public-worksheet-clear-all-style-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-clear-all-style-output.xlsx");
        const std::filesystem::path output_after_reacquire =
            artifact("fastxlsx-workbook-editor-public-worksheet-clear-all-reacquire-output.xlsx");
        fastxlsx::StyleId non_default_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_source);
            {
                fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
                non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
                styled_sheet.append_row({
                    fastxlsx::CellView::number(1.0).with_style(non_default_style),
                    fastxlsx::CellView::text("unstyled-clear-all"),
                });
            }
            writer.close();
        }

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(style_source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

        sheet.clear_cell_values();
        check(sheet.cell_count() == 2,
            "clear_cell_values() should keep every represented record as an explicit blank");
        check(sheet.contains_cell("A1") && sheet.contains_cell("B1"),
            "clear_cell_values() should keep represented coordinates present");
        check_cell_range_equals(sheet.used_range(), 1, 1, 1, 2,
            "clear_cell_values() should keep represented bounds over blank records");
        const fastxlsx::CellValue cleared_a1 = sheet.get_cell("A1");
        const fastxlsx::CellValue cleared_b1 = sheet.get_cell("B1");
        check(cleared_a1.kind() == fastxlsx::CellValueKind::Blank && cleared_a1.has_style()
                && cleared_a1.style_id().value() == non_default_style.value(),
            "clear_cell_values() should preserve source styles on styled blanks");
        check(cleared_b1.kind() == fastxlsx::CellValueKind::Blank && !cleared_b1.has_style(),
            "clear_cell_values() should keep unstyled source cells unstyled");
        check(sheet.has_pending_changes(),
            "clear_cell_values() should dirty a non-empty materialized worksheet");
        check(editor.pending_materialized_cell_count() == 2,
            "clear_cell_values() should keep explicit blanks in aggregate diagnostics");
        check(editor.pending_materialized_worksheet_names() ==
                std::vector<std::string> {"Styled"},
            "clear_cell_values() should expose the dirty materialized worksheet name");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1 && summaries[0].planned_name == "Styled",
                "clear_cell_values() should expose one dirty materialized summary");
            if (summaries.size() == 1) {
                check(summaries[0].materialized_dirty,
                    "clear_cell_values() summary should report materialized dirty state");
                check(summaries[0].materialized_cell_count == 2,
                    "clear_cell_values() summary should keep explicit blank records counted");
                check(summaries[0].estimated_materialized_memory_usage > 0,
                    "clear_cell_values() summary should expose materialized memory");
            }
        }
        check(!editor.last_edit_error().has_value(),
            "successful clear_cell_values() should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        const std::string styled_blank =
            R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("/>)";
        check_contains(worksheet_xml, R"(<dimension ref="A1:B1"/>)",
            "clear_cell_values() should keep the projected dimension over explicit blanks");
        check_contains(worksheet_xml, styled_blank,
            "clear_cell_values() should persist styled blanks with the source style id");
        check_contains(worksheet_xml, R"(<c r="B1"/>)",
            "clear_cell_values() should persist unstyled cells as blank records");
        check_not_contains(worksheet_xml, "unstyled-clear-all",
            "clear_cell_values() should omit old text payloads");
        check_not_contains(worksheet_xml, R"(<v>1</v>)",
            "clear_cell_values() should omit old numeric payloads");
        check(!sheet.has_pending_changes(),
            "save_as should clear dirty state after whole-store clear projection");
        check(editor.pending_materialized_worksheet_names().empty(),
            "save_as should clear dirty materialized names after whole-store clear");
        check(editor.pending_materialized_cell_count() == 0,
            "save_as should clear dirty materialized cell count after whole-store clear");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "save_as should clear dirty materialized memory after whole-store clear");
        check(editor.pending_worksheet_edits().empty(),
            "save_as should clear dirty materialized summaries after whole-store clear");

        fastxlsx::WorksheetEditor reacquired = editor.worksheet("Styled");
        check(!reacquired.has_pending_changes(),
            "reacquired worksheet after whole-store clear save should remain clean");
        check_cell_range_equals(reacquired.used_range(), 1, 1, 1, 2,
            "reacquired worksheet should keep explicit blank bounds after whole-store clear");
        reacquired.set_cell_value("A1", fastxlsx::CellValue::number(2.5));
        reacquired.set_cell_value("B1", fastxlsx::CellValue::text("after-clear-reacquire"));
        const fastxlsx::CellValue reacquired_a1 = reacquired.get_cell("A1");
        check(reacquired_a1.kind() == fastxlsx::CellValueKind::Number &&
                reacquired_a1.has_style() &&
                reacquired_a1.style_id().value() == non_default_style.value(),
            "reacquired value-only edit after whole-store clear should preserve source style");
        check(editor.pending_materialized_worksheet_names() ==
                std::vector<std::string> {"Styled"},
            "post-save whole-store clear handle reuse should dirty the same worksheet name");
        check(editor.pending_materialized_cell_count() == 2,
            "post-save whole-store clear handle reuse should report edited sparse cells");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1 && summaries[0].planned_name == "Styled",
                "post-save whole-store clear handle reuse should restore one dirty summary");
            if (summaries.size() == 1) {
                check(summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == 2,
                    "post-save whole-store clear dirty summary should match edited blanks");
            }
        }

        editor.save_as(output_after_reacquire);
        const auto reacquired_entries = fastxlsx::test::read_zip_entries(output_after_reacquire);
        const std::string reacquired_xml = reacquired_entries.at("xl/worksheets/sheet1.xml");
        check_contains(reacquired_xml, R"(<dimension ref="A1:B1"/>)",
            "reacquired whole-store clear session should keep edited bounds on second save_as");
        check_contains(reacquired_xml, R"(<c r="A1" s=")" +
                std::to_string(non_default_style.value()) + R"("><v>2.5</v></c>)",
            "reacquired whole-store clear session should persist styled value-only edit");
        check_contains(reacquired_xml, "after-clear-reacquire",
            "reacquired whole-store clear session should persist later text edits");
        check_not_contains(reacquired_xml, "unstyled-clear-all",
            "reacquired whole-store clear session should not revive cleared source text");
        check(editor.pending_materialized_worksheet_names().empty(),
            "second save_as should clear dirty names after whole-store clear handle reuse");
        check(editor.pending_materialized_cell_count() == 0,
            "second save_as should clear dirty cell count after whole-store clear handle reuse");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "second save_as should clear dirty memory after whole-store clear handle reuse");
        check(editor.pending_worksheet_edits().empty(),
            "second save_as should clear summaries after whole-store clear handle reuse");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-erase-all-output.xlsx");
        const std::filesystem::path output_after_reacquire =
            artifact("fastxlsx-workbook-editor-public-worksheet-erase-all-reacquire-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        sheet.set_cell("D4", fastxlsx::CellValue::text("erase-all-extra"));

        sheet.erase_cells();
        check(sheet.cell_count() == 0,
            "erase_cells() should remove every represented sparse record");
        check(!sheet.contains_cell("A1") && !sheet.contains_cell("D4"),
            "erase_cells() should make all prior coordinates missing");
        check(!sheet.used_range().has_value(),
            "erase_cells() should leave an empty represented range");
        check(sheet.has_pending_changes(),
            "erase_cells() should dirty a non-empty materialized worksheet");
        check(editor.pending_materialized_cell_count() == 0,
            "erase_cells() should report zero dirty materialized cells after full erase");
        check(editor.pending_materialized_worksheet_names() ==
                std::vector<std::string> {"Data"},
            "erase_cells() should still expose the dirty materialized worksheet name");
        check(editor.estimated_pending_materialized_memory_usage() > 0,
            "erase_cells() should keep dirty empty-store memory visible before save_as");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1 && summaries[0].planned_name == "Data",
                "erase_cells() should expose one dirty empty materialized summary");
            if (summaries.size() == 1) {
                check(summaries[0].materialized_dirty,
                    "erase_cells() summary should report materialized dirty state");
                check(summaries[0].materialized_cell_count == 0,
                    "erase_cells() summary should report zero sparse records after full erase");
                check(summaries[0].estimated_materialized_memory_usage > 0,
                    "erase_cells() summary should keep empty-store memory visible before save");
            }
        }
        check(!editor.last_edit_error().has_value(),
            "successful erase_cells() should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1"/>)",
            "erase_cells() should project an empty sparse worksheet dimension");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "erase_cells() should omit erased source A1 text");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "erase_cells() should omit erased source A2 text");
        check_not_contains(worksheet_xml, "erase-all-extra",
            "erase_cells() should omit erased newly inserted cells");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "erase_cells() should preserve untouched worksheets");
        check(!sheet.has_pending_changes(),
            "save_as should clear dirty state after whole-store erase projection");
        check(editor.pending_materialized_worksheet_names().empty(),
            "save_as should clear dirty materialized names after whole-store erase");
        check(editor.pending_materialized_cell_count() == 0,
            "save_as should clear dirty materialized cell count after whole-store erase");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "save_as should clear dirty materialized memory after whole-store erase");
        check(editor.pending_worksheet_edits().empty(),
            "save_as should clear dirty materialized summaries after whole-store erase");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-clear-all-noop"));
        }), "invalid mutation should seed last_edit_error before empty clear_cell_values()");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before empty clear_cell_values()");
        sheet.clear_cell_values();
        check(!editor.last_edit_error().has_value(),
            "empty clear_cell_values() should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty clear_cell_values() should not dirty a clean empty materialized worksheet");
        check(sheet.cell_count() == 0,
            "empty clear_cell_values() should keep the sparse store empty");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-erase-all-noop"));
        }), "invalid mutation should seed last_edit_error before empty erase_cells()");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before empty erase_cells()");
        sheet.erase_cells();
        check(!editor.last_edit_error().has_value(),
            "empty erase_cells() should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty erase_cells() should not dirty a clean empty materialized worksheet");
        check(sheet.cell_count() == 0,
            "empty erase_cells() should keep the sparse store empty");

        fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
        check(!reacquired.has_pending_changes(),
            "reacquired worksheet after whole-store erase save should remain clean");
        check(!reacquired.used_range().has_value(),
            "reacquired worksheet should remain empty after whole-store erase save");
        reacquired.append_row({
            fastxlsx::CellValue::text("after-erase-reacquire"),
            fastxlsx::CellValue::number(9.0),
        });
        check_cell_range_equals(reacquired.used_range(), 1, 1, 1, 2,
            "reacquired empty worksheet should append from row one");
        check(editor.pending_materialized_worksheet_names() ==
                std::vector<std::string> {"Data"},
            "post-save whole-store erase handle reuse should dirty the same worksheet name");
        check(editor.pending_materialized_cell_count() == 2,
            "post-save whole-store erase handle reuse should report appended sparse cells");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1 && summaries[0].planned_name == "Data",
                "post-save whole-store erase handle reuse should restore one dirty summary");
            if (summaries.size() == 1) {
                check(summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == 2,
                    "post-save whole-store erase dirty summary should match appended cells");
            }
        }

        editor.save_as(output_after_reacquire);
        const auto reacquired_entries = fastxlsx::test::read_zip_entries(output_after_reacquire);
        const std::string reacquired_xml = reacquired_entries.at("xl/worksheets/sheet1.xml");
        check_contains(reacquired_xml, R"(<dimension ref="A1:B1"/>)",
            "reacquired whole-store erase session should persist appended bounds");
        check_contains(reacquired_xml, "after-erase-reacquire",
            "reacquired whole-store erase session should persist later append text");
        check_contains(reacquired_xml, R"(<c r="B1"><v>9</v></c>)",
            "reacquired whole-store erase session should persist later append number");
        check_not_contains(reacquired_xml, "placeholder-a1",
            "reacquired whole-store erase session should not revive erased source text");
        check_not_contains(reacquired_xml, "erase-all-extra",
            "reacquired whole-store erase session should not revive erased dirty text");
        check(editor.pending_materialized_worksheet_names().empty(),
            "second save_as should clear dirty names after whole-store erase handle reuse");
        check(editor.pending_materialized_cell_count() == 0,
            "second save_as should clear dirty cell count after whole-store erase handle reuse");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "second save_as should clear dirty memory after whole-store erase handle reuse");
        check(editor.pending_worksheet_edits().empty(),
            "second save_as should clear summaries after whole-store erase handle reuse");
    }
}

void test_public_worksheet_editor_clear_row_preserves_sparse_records()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-clear-row-source.xlsx");

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-clear-row-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.clear_row(1);
        check(sheet.cell_count() == 3,
            "clear_row should keep represented cells as explicit blanks");
        check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
            "clear_row should convert target-row text cells to blanks");
        check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
            "clear_row should convert target-row numeric cells to blanks");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "clear_row should preserve represented cells outside the target row");
        check(sheet.has_pending_changes(),
            "clear_row should dirty the materialized worksheet when records are cleared");
        check(editor.pending_materialized_cell_count() == 3,
            "clear_row should keep blank records in aggregate diagnostics");
        check(!editor.last_edit_error().has_value(),
            "successful clear_row should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "clear_row should keep the projected dimension over explicit blanks");
        check_contains(worksheet_xml, R"(<c r="A1"/>)",
            "clear_row should persist row-one text as an explicit blank cell");
        check_contains(worksheet_xml, R"(<c r="B1"/>)",
            "clear_row should persist row-one number as an explicit blank cell");
        check_contains(worksheet_xml, "placeholder-a2",
            "clear_row should persist non-target row source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "clear_row should omit the old target-row text payload");
        check_not_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "clear_row should omit the old target-row numeric payload");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "clear_row should preserve untouched worksheets");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-clear-rows-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(4, 4, fastxlsx::CellValue::text("clear-row-range-extra"));
        sheet.clear_rows(1, 2);
        check(sheet.cell_count() == 4,
            "clear_rows should retain represented sparse records in all target rows");
        check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
            "clear_rows should clear first-row text cells");
        check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
            "clear_rows should clear first-row numeric cells");
        check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
            "clear_rows should clear second-row text cells");
        check(sheet.get_cell("D4").text_value() == "clear-row-range-extra",
            "clear_rows should preserve represented cells outside the row range");
        check(!editor.last_edit_error().has_value(),
            "successful clear_rows should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
            "clear_rows should keep explicit blanks in the projected dimension");
        check_contains(worksheet_xml, R"(<c r="A1"/>)",
            "clear_rows should persist row-one text as a blank");
        check_contains(worksheet_xml, R"(<c r="B1"/>)",
            "clear_rows should persist row-one number as a blank");
        check_contains(worksheet_xml, R"(<c r="A2"/>)",
            "clear_rows should persist row-two text as a blank");
        check_contains(worksheet_xml, "clear-row-range-extra",
            "clear_rows should persist non-target row edits");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "clear_rows should omit old row-one text payload");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "clear_rows should omit old row-two text payload");
        check_not_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "clear_rows should omit old row-one numeric payload");
    }

    {
        const std::filesystem::path style_source =
            artifact("fastxlsx-workbook-editor-public-worksheet-clear-row-style-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-clear-row-style-output.xlsx");
        fastxlsx::StyleId non_default_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_source);
            {
                fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
                non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
                styled_sheet.append_row({
                    fastxlsx::CellView::number(1.0).with_style(non_default_style),
                    fastxlsx::CellView::text("unstyled"),
                });
            }
            writer.close();
        }

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(style_source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");
        sheet.clear_row(1);

        const fastxlsx::CellValue cleared_a1 = sheet.get_cell("A1");
        const fastxlsx::CellValue cleared_b1 = sheet.get_cell("B1");
        check(cleared_a1.kind() == fastxlsx::CellValueKind::Blank && cleared_a1.has_style()
                && cleared_a1.style_id().value() == non_default_style.value(),
            "clear_row should preserve source style ids on blank records");
        check(cleared_b1.kind() == fastxlsx::CellValueKind::Blank && !cleared_b1.has_style(),
            "clear_row should keep unstyled source cells unstyled");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        const std::string styled_blank =
            R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("/>)";
        check_contains(worksheet_xml, styled_blank,
            "clear_row should persist styled blanks with the source style id");
        check_contains(worksheet_xml, R"(<c r="B1"/>)",
            "clear_row should persist unstyled source cells as unstyled blanks");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool invalid_failed = false;
        try {
            sheet.clear_row(0);
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_failed = true;
        }
        check(invalid_failed, "clear_row should reject invalid row numbers");
        check(editor.last_edit_error().has_value(),
            "failed clear_row invalid-row mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "clear_row invalid-row failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "clear_row invalid-row failure should preserve sparse cell count");

        bool reversed_failed = false;
        try {
            sheet.clear_rows(2, 1);
        } catch (const fastxlsx::FastXlsxError& error) {
            reversed_failed = true;
            check_contains(error.what(), "first_row <= last_row",
                "clear_rows reversed failure should expose the row ordering rule");
        }
        check(reversed_failed, "clear_rows should reject reversed row ranges");
        check(editor.last_edit_error().has_value(),
            "failed clear_rows reversed mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "clear_rows reversed failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "clear_rows reversed failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "clear_rows reversed failure should preserve existing cells");
    }
}

void test_public_worksheet_editor_clear_columns_noop_invalid_and_range()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-clear-column-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before missing clear_column no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before missing clear_column no-op");

        sheet.clear_column(3);
        check(!editor.last_edit_error().has_value(),
            "missing clear_column should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "missing clear_column should not dirty a clean materialized worksheet");
        check(!editor.has_pending_changes(),
            "missing clear_column should not make the editor dirty");
        check(sheet.cell_count() == 3,
            "missing clear_column should not create or remove sparse records");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-clear-column-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.clear_column(1);
        check(sheet.cell_count() == 3,
            "clear_column should keep represented column cells as explicit blanks");
        check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
            "clear_column should clear target-column row-one text cells");
        check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
            "clear_column should clear target-column row-two text cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "clear_column should preserve represented cells outside the target column");
        check(sheet.has_pending_changes(),
            "clear_column should dirty the materialized worksheet when records are cleared");
        check(editor.pending_materialized_cell_count() == 3,
            "clear_column should keep blank records in aggregate diagnostics");
        check(!editor.last_edit_error().has_value(),
            "successful clear_column should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "clear_column should keep the projected dimension over explicit blanks");
        check_contains(worksheet_xml, R"(<c r="A1"/>)",
            "clear_column should persist row-one text as a blank");
        check_contains(worksheet_xml, R"(<c r="A2"/>)",
            "clear_column should persist row-two text as a blank");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "clear_column should persist non-target column source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "clear_column should omit old row-one text payload");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "clear_column should omit old row-two text payload");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "clear_column should preserve untouched worksheets");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-clear-columns-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(4, 4, fastxlsx::CellValue::text("clear-column-range-extra"));
        sheet.clear_columns(1, 2);
        check(sheet.cell_count() == 4,
            "clear_columns should retain represented sparse records in all target columns");
        check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
            "clear_columns should clear first-column row-one text cells");
        check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
            "clear_columns should clear first-column row-two text cells");
        check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
            "clear_columns should clear second-column numeric cells");
        check(sheet.get_cell("D4").text_value() == "clear-column-range-extra",
            "clear_columns should preserve represented cells outside the column range");
        check(!editor.last_edit_error().has_value(),
            "successful clear_columns should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
            "clear_columns should keep explicit blanks in the projected dimension");
        check_contains(worksheet_xml, R"(<c r="A1"/>)",
            "clear_columns should persist first-column row-one text as a blank");
        check_contains(worksheet_xml, R"(<c r="A2"/>)",
            "clear_columns should persist first-column row-two text as a blank");
        check_contains(worksheet_xml, R"(<c r="B1"/>)",
            "clear_columns should persist second-column number as a blank");
        check_contains(worksheet_xml, "clear-column-range-extra",
            "clear_columns should persist non-target column edits");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "clear_columns should omit old first-column row-one text payload");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "clear_columns should omit old first-column row-two text payload");
        check_not_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "clear_columns should omit old second-column numeric payload");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool invalid_failed = false;
        try {
            sheet.clear_column(0);
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_failed = true;
        }
        check(invalid_failed, "clear_column should reject invalid column numbers");
        check(editor.last_edit_error().has_value(),
            "failed clear_column invalid-column mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "clear_column invalid-column failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "clear_column invalid-column failure should preserve sparse cell count");

        bool reversed_failed = false;
        try {
            sheet.clear_columns(2, 1);
        } catch (const fastxlsx::FastXlsxError& error) {
            reversed_failed = true;
            check_contains(error.what(), "first_column <= last_column",
                "clear_columns reversed failure should expose the column ordering rule");
        }
        check(reversed_failed, "clear_columns should reject reversed column ranges");
        check(editor.last_edit_error().has_value(),
            "failed clear_columns reversed mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "clear_columns reversed failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "clear_columns reversed failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "clear_columns reversed failure should preserve existing cells");
    }
}

void test_public_worksheet_editor_erase_row_removes_sparse_row()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-erase-row-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-row-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.erase_row(1);
    check(sheet.cell_count() == 1,
        "erase_row should remove all represented cells in the target sparse row");
    check(!sheet.try_cell("A1").has_value(),
        "erase_row should remove target-row text cells");
    check(!sheet.try_cell("B1").has_value(),
        "erase_row should remove target-row numeric cells");
    check(sheet.get_cell("A2").text_value() == "placeholder-a2",
        "erase_row should preserve represented cells outside the target row");
    check(sheet.has_pending_changes(),
        "erase_row should dirty the materialized worksheet when records are removed");
    check(editor.pending_materialized_cell_count() == 1,
        "erase_row should update aggregate materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "successful erase_row should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A2"/>)",
        "erase_row should shrink the projected dimension to remaining sparse cells");
    check_contains(worksheet_xml, "placeholder-a2",
        "erase_row should persist non-target row source cells");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "erase_row should omit erased row text cells");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "erase_row should omit erased row numeric cells");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "erase_row should preserve untouched worksheets");
}

void test_public_worksheet_editor_erase_rows_noop_invalid_and_range()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-erase-rows-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before missing erase_row no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before missing erase_row no-op");

        sheet.erase_row(3);
        check(!editor.last_edit_error().has_value(),
            "missing erase_row should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "missing erase_row should not dirty a clean materialized worksheet");
        check(!editor.has_pending_changes(),
            "missing erase_row should not make the editor dirty");
        check(sheet.cell_count() == 3,
            "missing erase_row should not create or remove sparse records");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-erase-rows-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(4, 4, fastxlsx::CellValue::text("range-extra"));
        sheet.erase_rows(1, 2);
        check(sheet.cell_count() == 1,
            "erase_rows should remove represented sparse records in all target rows");
        check(!sheet.try_cell("A1").has_value(),
            "erase_rows should remove first row text cells");
        check(!sheet.try_cell("B1").has_value(),
            "erase_rows should remove first row numeric cells");
        check(!sheet.try_cell("A2").has_value(),
            "erase_rows should remove second row text cells");
        check(sheet.get_cell("D4").text_value() == "range-extra",
            "erase_rows should preserve represented cells outside the row range");
        check(sheet.has_pending_changes(),
            "erase_rows should keep the materialized worksheet dirty after mutation");
        check(!editor.last_edit_error().has_value(),
            "successful erase_rows should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="D4"/>)",
            "erase_rows should shrink the projected dimension to remaining sparse cells");
        check_contains(worksheet_xml, "range-extra",
            "erase_rows should persist non-target row edits");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "erase_rows should omit erased row-one text cells");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "erase_rows should omit erased row-two text cells");
        check_not_contains(worksheet_xml, R"(r="B1")",
            "erase_rows should omit erased row-one numeric cells");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool invalid_failed = false;
        try {
            sheet.erase_row(0);
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_failed = true;
        }
        check(invalid_failed, "erase_row should reject invalid row numbers");
        check(editor.last_edit_error().has_value(),
            "failed erase_row invalid-row mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "erase_row invalid-row failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "erase_row invalid-row failure should preserve sparse cell count");

        bool reversed_failed = false;
        try {
            sheet.erase_rows(2, 1);
        } catch (const fastxlsx::FastXlsxError& error) {
            reversed_failed = true;
            check_contains(error.what(), "first_row <= last_row",
                "erase_rows reversed failure should expose the row ordering rule");
        }
        check(reversed_failed, "erase_rows should reject reversed row ranges");
        check(editor.last_edit_error().has_value(),
            "failed erase_rows reversed mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "erase_rows reversed failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "erase_rows reversed failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "erase_rows reversed failure should preserve existing cells");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-erase-row-budget-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

        sheet.erase_row(1);
        check(sheet.cell_count() == 1,
            "erase_row should release sparse records under an exact max_cells budget");
        sheet.set_cell(3, 1, fastxlsx::CellValue::text("after-erase-row-budget"));
        check(!editor.last_edit_error().has_value(),
            "successful set_cell after erase_row budget release should clear diagnostics");
        check(sheet.cell_count() == 2,
            "set_cell after erase_row should stay within the exact max_cells budget");
        check(sheet.get_cell("A3").text_value() == "after-erase-row-budget",
            "set_cell after erase_row should insert the new sparse record");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "after-erase-row-budget",
            "budget-released insertion after erase_row should persist through save_as");
        check_contains(worksheet_xml, "placeholder-a2",
            "budget-released insertion should preserve non-target row source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "budget-released insertion should not resurrect erased row text cells");
        check_not_contains(worksheet_xml, R"(r="B1")",
            "budget-released insertion should not resurrect erased row numeric cells");
    }
}

void test_public_worksheet_editor_erase_column_removes_sparse_column()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-erase-column-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-column-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.erase_column(1);
    check(sheet.cell_count() == 1,
        "erase_column should remove all represented cells in the target sparse column");
    check(!sheet.try_cell("A1").has_value(),
        "erase_column should remove target-column row-one text cells");
    check(!sheet.try_cell("A2").has_value(),
        "erase_column should remove target-column row-two text cells");
    check(sheet.get_cell("B1").number_value() == 1.0,
        "erase_column should preserve represented cells outside the target column");
    check(sheet.has_pending_changes(),
        "erase_column should dirty the materialized worksheet when records are removed");
    check(editor.pending_materialized_cell_count() == 1,
        "erase_column should update aggregate materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "successful erase_column should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="B1"/>)",
        "erase_column should shrink the projected dimension to remaining sparse cells");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "erase_column should persist non-target column source cells");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "erase_column should omit erased column row-one text cells");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "erase_column should omit erased column row-two text cells");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "erase_column should preserve untouched worksheets");
}

void test_public_worksheet_editor_erase_columns_noop_invalid_and_range()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-erase-columns-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before missing erase_column no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before missing erase_column no-op");

        sheet.erase_column(3);
        check(!editor.last_edit_error().has_value(),
            "missing erase_column should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "missing erase_column should not dirty a clean materialized worksheet");
        check(!editor.has_pending_changes(),
            "missing erase_column should not make the editor dirty");
        check(sheet.cell_count() == 3,
            "missing erase_column should not create or remove sparse records");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-erase-columns-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(4, 4, fastxlsx::CellValue::text("column-range-extra"));
        sheet.erase_columns(1, 2);
        check(sheet.cell_count() == 1,
            "erase_columns should remove represented sparse records in all target columns");
        check(!sheet.try_cell("A1").has_value(),
            "erase_columns should remove first column row-one text cells");
        check(!sheet.try_cell("A2").has_value(),
            "erase_columns should remove first column row-two text cells");
        check(!sheet.try_cell("B1").has_value(),
            "erase_columns should remove second column numeric cells");
        check(sheet.get_cell("D4").text_value() == "column-range-extra",
            "erase_columns should preserve represented cells outside the column range");
        check(sheet.has_pending_changes(),
            "erase_columns should keep the materialized worksheet dirty after mutation");
        check(!editor.last_edit_error().has_value(),
            "successful erase_columns should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="D4"/>)",
            "erase_columns should shrink the projected dimension to remaining sparse cells");
        check_contains(worksheet_xml, "column-range-extra",
            "erase_columns should persist non-target column edits");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "erase_columns should omit erased column row-one text cells");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "erase_columns should omit erased column row-two text cells");
        check_not_contains(worksheet_xml, R"(r="B1")",
            "erase_columns should omit erased second-column numeric cells");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool invalid_failed = false;
        try {
            sheet.erase_column(0);
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_failed = true;
        }
        check(invalid_failed, "erase_column should reject invalid column numbers");
        check(editor.last_edit_error().has_value(),
            "failed erase_column invalid-column mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "erase_column invalid-column failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "erase_column invalid-column failure should preserve sparse cell count");

        bool reversed_failed = false;
        try {
            sheet.erase_columns(2, 1);
        } catch (const fastxlsx::FastXlsxError& error) {
            reversed_failed = true;
            check_contains(error.what(), "first_column <= last_column",
                "erase_columns reversed failure should expose the column ordering rule");
        }
        check(reversed_failed, "erase_columns should reject reversed column ranges");
        check(editor.last_edit_error().has_value(),
            "failed erase_columns reversed mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "erase_columns reversed failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "erase_columns reversed failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "erase_columns reversed failure should preserve existing cells");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-erase-column-budget-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

        sheet.erase_column(1);
        check(sheet.cell_count() == 1,
            "erase_column should release sparse records under an exact max_cells budget");
        sheet.set_cell(3, 1, fastxlsx::CellValue::text("after-erase-column-budget"));
        check(!editor.last_edit_error().has_value(),
            "successful set_cell after erase_column budget release should clear diagnostics");
        check(sheet.cell_count() == 2,
            "set_cell after erase_column should stay within the exact max_cells budget");
        check(sheet.get_cell("A3").text_value() == "after-erase-column-budget",
            "set_cell after erase_column should insert the new sparse record");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "after-erase-column-budget",
            "budget-released insertion after erase_column should persist through save_as");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "budget-released insertion should preserve non-target column source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "budget-released insertion should not resurrect erased column row-one text cells");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "budget-released insertion should not resurrect erased column row-two text cells");
    }
}

void test_public_worksheet_editor_insert_rows_shifts_sparse_records()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-insert-rows-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-rows-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(3, 3, fastxlsx::CellValue::text("extra-c3"));
    sheet.insert_rows(2, 2);

    check(sheet.cell_count() == 8,
        "insert_rows should preserve sparse cell count when it only shifts records");
    const std::optional<fastxlsx::CellValue> shifted_a1 = sheet.try_cell("A1");
    check(shifted_a1.has_value() && shifted_a1->text_value() == "placeholder-a1",
        "insert_rows should preserve cells above the insertion point");
    const std::optional<fastxlsx::CellValue> shifted_b1 = sheet.try_cell("B1");
    check(shifted_b1.has_value() && shifted_b1->number_value() == 1.0,
        "insert_rows should preserve same-row cells above the insertion point");
    const std::optional<fastxlsx::CellValue> shifted_a4 = sheet.try_cell("A4");
    check(shifted_a4.has_value() && shifted_a4->text_value() == "placeholder-a2",
        "insert_rows should shift source-backed cells downward by row_count");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D4");
    check(shifted_formula.has_value()
            && shifted_formula->kind() == fastxlsx::CellValueKind::Formula
            && shifted_formula->text_value() == "A3+B3"
            && shifted_formula->has_style()
            && shifted_formula->style_id().value() == styled_formula_style.value(),
        "insert_rows should translate moved formula text and preserve the source style id");
    const std::optional<fastxlsx::CellValue> shifted_filler = sheet.try_cell("B4");
    check(shifted_filler.has_value() && shifted_filler->text_value() == "row2-gap-b2",
        "insert_rows should preserve the shifted second-row filler cell");
    const std::optional<fastxlsx::CellValue> shifted_a5 = sheet.try_cell("A5");
    check(shifted_a5.has_value() && shifted_a5->text_value() == "extra-c3",
        "insert_rows should shift source-backed trailing cells downward by row_count");
    const std::optional<fastxlsx::CellValue> shifted_c5 = sheet.try_cell("C5");
    check(shifted_c5.has_value() && shifted_c5->text_value() == "extra-c3",
        "insert_rows should shift dirty cells downward by row_count");
    check(!sheet.try_cell("A2").has_value(),
        "insert_rows should leave the inserted sparse row without synthesized cells");
    check(!sheet.try_cell("D2").has_value(),
        "insert_rows should remove the old shifted formula coordinate");
    check(!sheet.try_cell("C3").has_value(),
        "insert_rows should remove the old shifted sparse coordinate");
    check_cell_range_equals(sheet.used_range(), 1, 1, 5, 4,
        "insert_rows should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_four = sheet.row_cells(4);
    check(shifted_row_four.size() == 4,
        "insert_rows row_cells should expose the shifted row snapshot");
    check(shifted_row_four[0].reference.row == 4 && shifted_row_four[0].reference.column == 1 &&
            shifted_row_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_four[0].value.text_value() == "placeholder-a2",
        "insert_rows row_cells should keep the shifted source-backed cell first");
    check(shifted_row_four[1].reference.row == 4 && shifted_row_four[1].reference.column == 2 &&
            shifted_row_four[1].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_four[1].value.text_value() == "row2-gap-b2",
        "insert_rows row_cells should keep the shifted filler cell in column order");
    check(shifted_row_four[2].reference.row == 4 && shifted_row_four[2].reference.column == 3 &&
            shifted_row_four[2].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_four[2].value.text_value() == "row2-gap-c2",
        "insert_rows row_cells should keep the second shifted filler cell in column order");
    check(shifted_row_four[3].reference.row == 4 && shifted_row_four[3].reference.column == 4 &&
            shifted_row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_four[3].value.text_value() == "A3+B3" &&
            shifted_row_four[3].value.has_style() &&
            shifted_row_four[3].value.style_id().value() == styled_formula_style.value(),
        "insert_rows row_cells should keep the translated formula cell and style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_four =
        sheet.column_cells(4);
    check(shifted_column_four.size() == 1,
        "insert_rows column_cells should expose the shifted formula column snapshot");
    check(shifted_column_four[0].reference.row == 4 &&
            shifted_column_four[0].reference.column == 4 &&
            shifted_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_four[0].value.text_value() == "A3+B3" &&
            shifted_column_four[0].value.has_style() &&
            shifted_column_four[0].value.style_id().value() == styled_formula_style.value(),
        "insert_rows column_cells should keep the translated formula cell and style id");
    check(sheet.has_pending_changes(),
        "insert_rows should dirty the materialized worksheet when records shift");
    check(editor.pending_materialized_cell_count() == 8,
        "insert_rows should keep aggregate materialized cell count stable");
    check(!editor.last_edit_error().has_value(),
        "successful insert_rows should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A3+B3</f></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "insert_rows save_as should project the shifted sparse dimension");
    check_contains(worksheet_xml, styled_formula_xml,
        "insert_rows save_as should write the translated formula cell with the preserved style id");
    check_contains(worksheet_xml, R"(<c r="A4")",
        "insert_rows save_as should write the shifted source-backed row coordinate");
    check_contains(worksheet_xml, R"(<c r="B4")",
        "insert_rows save_as should write the shifted filler cell");
    check_contains(worksheet_xml, R"(<c r="A5")",
        "insert_rows save_as should write the shifted source-backed trailing row coordinate");
    check_contains(worksheet_xml, R"(<c r="C5")",
        "insert_rows save_as should write the shifted dirty row coordinate");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "insert_rows save_as should not keep the old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "insert_rows save_as should not keep the old source-backed row coordinate");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "insert_rows save_as should not keep the old dirty row coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "insert_rows should preserve untouched worksheets");
}

void test_public_worksheet_editor_insert_rows_shifted_sparse_snapshot()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-insert-rows-snapshot-source.xlsx",
            styled_formula_style);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 2);

    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells();
    check(cells.size() == 7,
        "insert_rows sparse_cells should keep the shifted sparse record count");
    check(cells[0].reference.row == 1 && cells[0].reference.column == 1 &&
            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[0].value.text_value() == "placeholder-a1",
        "insert_rows sparse_cells should keep row-major source-backed A1 first");
    check(cells[1].reference.row == 1 && cells[1].reference.column == 2 &&
            cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
            cells[1].value.number_value() == 1.0,
        "insert_rows sparse_cells should keep same-row cells after A1");
    check(cells[2].reference.row == 4 && cells[2].reference.column == 1 &&
            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[2].value.text_value() == "placeholder-a2",
        "insert_rows sparse_cells should expose the shifted source-backed row");
    check(cells[3].reference.row == 4 && cells[3].reference.column == 2 &&
            cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[3].value.text_value() == "row2-gap-b2",
        "insert_rows sparse_cells should preserve the shifted filler cell");
    check(cells[4].reference.row == 4 && cells[4].reference.column == 3 &&
            cells[4].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[4].value.text_value() == "row2-gap-c2",
        "insert_rows sparse_cells should preserve the other shifted filler cell");
    check(cells[5].reference.row == 4 && cells[5].reference.column == 4 &&
            cells[5].value.kind() == fastxlsx::CellValueKind::Formula &&
            cells[5].value.text_value() == "A3+B3" &&
            cells[5].value.has_style() &&
            cells[5].value.style_id().value() == styled_formula_style.value(),
        "insert_rows sparse_cells should translate the shifted formula cell and keep its style");
    check(cells[6].reference.row == 5 && cells[6].reference.column == 1 &&
            cells[6].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[6].value.text_value() == "extra-c3",
        "insert_rows sparse_cells should keep later dirty rows after the shift");
}

void test_public_worksheet_editor_delete_rows_shifts_sparse_records()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-delete-rows-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 3, fastxlsx::CellValue::formula("A2+B4"));
    sheet.set_cell(4, 2, fastxlsx::CellValue::text("tail-b4"));
    sheet.delete_rows(1, 1);

    check(sheet.cell_count() == 3,
        "delete_rows should remove represented records in the deleted sparse rows");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2",
        "delete_rows should shift later source-backed rows upward");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("C3");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula
            && shifted_formula.text_value() == "A1+B3",
        "delete_rows should translate moved formula text by the row shift");
    check(sheet.get_cell("B3").text_value() == "tail-b4",
        "delete_rows should shift later dirty rows upward");
    check(!sheet.try_cell("B1").has_value(),
        "delete_rows should remove represented cells from the deleted row");
    check(!sheet.try_cell("A2").has_value(),
        "delete_rows should remove old shifted source coordinates");
    check(!sheet.try_cell("C4").has_value(),
        "delete_rows should remove the old shifted formula coordinate");
    check(!sheet.try_cell("B4").has_value(),
        "delete_rows should remove old shifted dirty coordinates");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 3,
        "delete_rows should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_three = sheet.row_cells(3);
    check(shifted_row_three.size() == 2,
        "delete_rows row_cells should expose the shifted row snapshot");
    check(shifted_row_three[0].reference.row == 3 && shifted_row_three[0].reference.column == 2 &&
            shifted_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_three[0].value.text_value() == "tail-b4",
        "delete_rows row_cells should keep the shifted dirty cell first");
    check(shifted_row_three[1].reference.row == 3 && shifted_row_three[1].reference.column == 3 &&
            shifted_row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_three[1].value.text_value() == "A1+B3",
        "delete_rows row_cells should keep the translated formula cell second");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_three =
        sheet.column_cells(3);
    check(shifted_column_three.size() == 1,
        "delete_rows column_cells should expose the shifted formula column snapshot");
    check(shifted_column_three[0].reference.row == 3 &&
            shifted_column_three[0].reference.column == 3 &&
            shifted_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_three[0].value.text_value() == "A1+B3",
        "delete_rows column_cells should keep the translated formula cell");
    check(!editor.last_edit_error().has_value(),
        "successful delete_rows should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "delete_rows save_as should project the shifted sparse dimension");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "delete_rows save_as should write the shifted source-backed cell");
    check_contains(worksheet_xml, R"(<c r="C3"><f>A1+B3</f></c>)",
        "delete_rows save_as should write the translated formula cell");
    check_contains(worksheet_xml, R"(<c r="B3")",
        "delete_rows save_as should write the shifted dirty cell");
    check_not_contains(worksheet_xml, R"(r="C4")",
        "delete_rows save_as should not keep the old formula coordinate");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "delete_rows save_as should omit deleted row text cells");
    check_not_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "delete_rows save_as should omit deleted row numeric cells");
}

void test_public_worksheet_editor_insert_columns_shifts_sparse_records()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-insert-columns-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(2, 3, fastxlsx::CellValue::formula("A1+B1"));
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("extra-c3"));
    sheet.insert_columns(2, 2);

    check(sheet.cell_count() == 5,
        "insert_columns should preserve sparse cell count when it only shifts records");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "insert_columns should preserve cells left of the insertion point");
    check(sheet.get_cell("A2").text_value() == "placeholder-a2",
        "insert_columns should preserve lower cells left of the insertion point");
    check(sheet.get_cell("D1").number_value() == 1.0,
        "insert_columns should shift source-backed cells right by column_count");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("E2");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula
            && shifted_formula.text_value() == "C1+D1",
        "insert_columns should translate moved formula text by the column shift");
    check(sheet.get_cell("E3").text_value() == "extra-c3",
        "insert_columns should shift dirty cells right by column_count");
    check(!sheet.try_cell("B1").has_value(),
        "insert_columns should leave inserted sparse columns without synthesized cells");
    check(!sheet.try_cell("C2").has_value(),
        "insert_columns should remove the old shifted formula coordinate");
    check(!sheet.try_cell("C3").has_value(),
        "insert_columns should remove the old shifted dirty coordinate");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 5,
        "insert_columns should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_two = sheet.row_cells(2);
    check(shifted_row_two.size() == 2,
        "insert_columns row_cells should expose the shifted row snapshot");
    check(shifted_row_two[0].reference.row == 2 && shifted_row_two[0].reference.column == 1 &&
            shifted_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_two[0].value.text_value() == "placeholder-a2",
        "insert_columns row_cells should keep the source-backed cell first");
    check(shifted_row_two[1].reference.row == 2 && shifted_row_two[1].reference.column == 5 &&
            shifted_row_two[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_two[1].value.text_value() == "C1+D1",
        "insert_columns row_cells should keep the translated formula cell second");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_five =
        sheet.column_cells(5);
    check(shifted_column_five.size() == 2,
        "insert_columns column_cells should expose the shifted formula column snapshot");
    check(shifted_column_five[0].reference.row == 2 &&
            shifted_column_five[0].reference.column == 5 &&
            shifted_column_five[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_five[0].value.text_value() == "C1+D1",
        "insert_columns column_cells should keep the translated formula cell first");
    check(shifted_column_five[1].reference.row == 3 &&
            shifted_column_five[1].reference.column == 5 &&
            shifted_column_five[1].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_column_five[1].value.text_value() == "extra-c3",
        "insert_columns column_cells should keep the shifted dirty cell second");
    check(!editor.last_edit_error().has_value(),
        "successful insert_columns should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E3"/>)",
        "insert_columns save_as should project the shifted sparse dimension");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "insert_columns save_as should write the shifted source-backed numeric cell");
    check_contains(worksheet_xml, R"(<c r="E2"><f>C1+D1</f></c>)",
        "insert_columns save_as should write the translated formula cell");
    check_contains(worksheet_xml, R"(<c r="E3")",
        "insert_columns save_as should write the shifted dirty cell");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "insert_columns save_as should not keep the old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "insert_columns save_as should not keep the old shifted source coordinate");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "insert_columns save_as should not keep the old shifted dirty coordinate");
}

void test_public_worksheet_editor_delete_columns_shifts_sparse_records()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-delete-columns-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(1, 3, fastxlsx::CellValue::formula("B2+D1"));
    sheet.set_cell(2, 4, fastxlsx::CellValue::text("tail-d2"));
    sheet.delete_columns(1, 1);

    check(sheet.cell_count() == 3,
        "delete_columns should remove represented records in the deleted sparse columns");
    check(sheet.get_cell("A1").number_value() == 1.0,
        "delete_columns should shift later source-backed columns left");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("B1");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula
            && shifted_formula.text_value() == "A2+C1",
        "delete_columns should translate moved formula text by the column shift");
    check(sheet.get_cell("C2").text_value() == "tail-d2",
        "delete_columns should shift later dirty columns left");
    check(!sheet.try_cell("A2").has_value(),
        "delete_columns should remove represented cells from the deleted column");
    check(!sheet.try_cell("C1").has_value(),
        "delete_columns should remove the old shifted formula coordinate");
    check(!sheet.try_cell("D2").has_value(),
        "delete_columns should remove old shifted dirty coordinates");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "delete_columns should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_one = sheet.row_cells(1);
    check(shifted_row_one.size() == 2,
        "delete_columns row_cells should expose the shifted row snapshot");
    check(shifted_row_one[0].reference.row == 1 && shifted_row_one[0].reference.column == 1 &&
            shifted_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
            shifted_row_one[0].value.number_value() == 1.0,
        "delete_columns row_cells should keep the source-backed cell first");
    check(shifted_row_one[1].reference.row == 1 && shifted_row_one[1].reference.column == 2 &&
            shifted_row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_one[1].value.text_value() == "A2+C1",
        "delete_columns row_cells should keep the translated formula cell second");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_two =
        sheet.column_cells(2);
    check(shifted_column_two.size() == 1,
        "delete_columns column_cells should expose the shifted formula column snapshot");
    check(shifted_column_two[0].reference.row == 1 &&
            shifted_column_two[0].reference.column == 2 &&
            shifted_column_two[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_two[0].value.text_value() == "A2+C1",
        "delete_columns column_cells should keep the translated formula cell");
    check(!editor.last_edit_error().has_value(),
        "successful delete_columns should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "delete_columns save_as should project the shifted sparse dimension");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "delete_columns save_as should write the shifted source-backed numeric cell");
    check_contains(worksheet_xml, R"(<c r="B1"><f>A2+C1</f></c>)",
        "delete_columns save_as should write the translated formula cell");
    check_contains(worksheet_xml, R"(<c r="C2")",
        "delete_columns save_as should write the shifted dirty cell");
    check_not_contains(worksheet_xml, R"(r="C1")",
        "delete_columns save_as should not keep the old formula coordinate");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "delete_columns save_as should omit deleted column row-one text cells");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "delete_columns save_as should omit deleted column row-two text cells");
}

void test_public_worksheet_editor_shift_formula_out_of_bounds_references()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-formula-ref-source.xlsx");

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-formula-row-ref-output.xlsx");
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(4, 3, fastxlsx::CellValue::formula("A1+A:A+1:1+B4"));
        sheet.delete_rows(1, 1);

        const fastxlsx::CellValue shifted_formula = sheet.get_cell("C3");
        check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula.text_value() == "#REF!+A:A+#REF!+B3",
            "delete_rows should translate row-out-of-bounds formula references to #REF!");
        check(!sheet.try_cell("C4").has_value(),
            "delete_rows formula #REF translation should remove the old formula coordinate");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml,
            R"(<c r="C3"><f>#REF!+A:A+#REF!+B3</f></c>)",
            "delete_rows save_as should persist row-out-of-bounds formula references as #REF!");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-formula-column-ref-output.xlsx");
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(1, 4, fastxlsx::CellValue::formula("A1+A:A+1:1+D2"));
        sheet.delete_columns(1, 1);

        const fastxlsx::CellValue shifted_formula = sheet.get_cell("C1");
        check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula.text_value() == "#REF!+#REF!+1:1+C2",
            "delete_columns should translate column-out-of-bounds formula references to #REF!");
        check(!sheet.try_cell("D1").has_value(),
            "delete_columns formula #REF translation should remove the old formula coordinate");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml,
            R"(<c r="C1"><f>#REF!+#REF!+1:1+C2</f></c>)",
            "delete_columns save_as should persist column-out-of-bounds formula references as #REF!");
    }
}

void test_public_worksheet_editor_row_column_shift_noop_and_invalid_preserve_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-guards-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before shift zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before shift zero-count no-op");

        sheet.insert_rows(2, 0);
        sheet.delete_rows(2, 0);
        sheet.insert_columns(2, 0);
        sheet.delete_columns(2, 0);
        check(!editor.last_edit_error().has_value(),
            "zero-count row/column shifts should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "zero-count row/column shifts should not dirty a clean materialized worksheet");
        check(sheet.cell_count() == 3,
            "zero-count row/column shifts should preserve sparse cell count");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-nonzero-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before shift nonzero no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before shift nonzero no-op");

        sheet.insert_rows(10, 1);
        sheet.insert_columns(10, 1);
        sheet.delete_rows(10, 1);
        sheet.delete_columns(10, 1);
        check(!editor.last_edit_error().has_value(),
            "nonzero row/column shift no-ops should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "nonzero row/column shift no-ops should not dirty a clean materialized worksheet");
        check(sheet.cell_count() == 3,
            "nonzero row/column shift no-ops should preserve sparse cell count");
        check(editor.pending_materialized_cell_count() == 0,
            "nonzero row/column shift no-ops should not contribute pending materialized cells");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "nonzero row/column shift no-ops should preserve source-backed cells");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "save_as after nonzero row/column shift no-ops should copy source entries");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool invalid_row_failed = false;
        try {
            sheet.insert_rows(0, 1);
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_row_failed = true;
        }
        check(invalid_row_failed, "insert_rows should reject invalid row numbers");
        check(editor.last_edit_error().has_value(),
            "failed insert_rows invalid-row mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_rows invalid-row failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_rows invalid-row failure should preserve sparse cell count");

        bool invalid_delete_rows_count_failed = false;
        try {
            sheet.delete_rows(1048576, 2);
        } catch (const fastxlsx::FastXlsxError& error) {
            invalid_delete_rows_count_failed = true;
            check_contains(error.what(), "1048576",
                "delete_rows invalid count should expose the Excel row limit");
        }
        check(invalid_delete_rows_count_failed,
            "delete_rows should reject count ranges past the Excel row limit");
        check(!sheet.has_pending_changes(),
            "delete_rows invalid-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "delete_rows invalid-count failure should preserve sparse cell count");

        bool invalid_column_failed = false;
        try {
            sheet.insert_columns(0, 1);
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_column_failed = true;
        }
        check(invalid_column_failed,
            "insert_columns should reject invalid column numbers");
        check(editor.last_edit_error().has_value(),
            "failed insert_columns invalid-column mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_columns invalid-column failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_columns invalid-column failure should preserve sparse cell count");

        bool invalid_count_failed = false;
        try {
            sheet.delete_columns(16384, 2);
        } catch (const fastxlsx::FastXlsxError& error) {
            invalid_count_failed = true;
            check_contains(error.what(), "16384",
                "delete_columns invalid count should expose the Excel column limit");
        }
        check(invalid_count_failed,
            "delete_columns should reject count ranges past the Excel column limit");
        check(!sheet.has_pending_changes(),
            "delete_columns invalid-count failure should not dirty the materialized worksheet");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "delete_columns invalid-count failure should preserve source cells");
        check(editor.pending_materialized_cell_count() == 0,
            "shift validation failures should not contribute pending materialized cells");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "save_as after shift validation failures should copy source entries");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(1048576, 1, fastxlsx::CellValue::text("row-edge"));
        const std::size_t dirty_count = sheet.cell_count();
        bool row_overflow_failed = false;
        try {
            sheet.insert_rows(1, 1);
        } catch (const fastxlsx::FastXlsxError& error) {
            row_overflow_failed = true;
            check_contains(error.what(), "1048576",
                "insert_rows overflow should expose the Excel row limit");
        }
        check(row_overflow_failed,
            "insert_rows should reject shifts that move a represented cell past the row limit");
        check(sheet.cell_count() == dirty_count,
            "insert_rows overflow failure should preserve sparse cell count");
        check(sheet.get_cell("A1048576").text_value() == "row-edge",
            "insert_rows overflow failure should preserve the edge cell");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "insert_rows overflow failure should not shift earlier cells");
        check(sheet.has_pending_changes(),
            "insert_rows overflow failure should preserve prior dirty state");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(1, 16384, fastxlsx::CellValue::text("column-edge"));
        const std::size_t dirty_count = sheet.cell_count();
        bool column_overflow_failed = false;
        try {
            sheet.insert_columns(1, 1);
        } catch (const fastxlsx::FastXlsxError& error) {
            column_overflow_failed = true;
            check_contains(error.what(), "16384",
                "insert_columns overflow should expose the Excel column limit");
        }
        check(column_overflow_failed,
            "insert_columns should reject shifts that move a represented cell past the column limit");
        check(sheet.cell_count() == dirty_count,
            "insert_columns overflow failure should preserve sparse cell count");
        check(sheet.get_cell("XFD1").text_value() == "column-edge",
            "insert_columns overflow failure should preserve the edge cell");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "insert_columns overflow failure should not shift earlier cells");
        check(sheet.has_pending_changes(),
            "insert_columns overflow failure should preserve prior dirty state");
    }
}

void test_public_worksheet_editor_shift_memory_guard_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_shift_memory_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-memory-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-memory-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
    const std::size_t exact_memory_budget = sizing_sheet.estimated_memory_usage();
    const std::size_t baseline_count = sizing_sheet.cell_count();

    fastxlsx::WorksheetEditorOptions options;
    options.memory_budget_bytes = exact_memory_budget;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    const std::size_t baseline_memory = sheet.estimated_memory_usage();

    bool failed = false;
    try {
        sheet.insert_rows(2, 1);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "insert_rows formula translation should expose memory budget diagnostics");
    }
    check(failed,
        "insert_rows should reject formula translation that exceeds memory budget");
    check(editor.last_edit_error().has_value(),
        "failed insert_rows memory-budget mutation should update last_edit_error");
    check_contains(*editor.last_edit_error(), "CellStore memory_budget_bytes guardrail exceeded",
        "last_edit_error should retain the insert_rows memory-budget diagnostic");
    check(!sheet.has_pending_changes(),
        "failed insert_rows memory-budget mutation should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "failed insert_rows memory-budget mutation should not dirty the editor");
    check(editor.pending_materialized_cell_count() == 0,
        "failed insert_rows memory-budget mutation should not expose dirty materialized cells");
    check(sheet.cell_count() == baseline_count,
        "failed insert_rows memory-budget mutation should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        "failed insert_rows memory-budget mutation should preserve sparse memory estimate");
    const fastxlsx::CellValue original_formula = sheet.get_cell("A2");
    check(original_formula.kind() == fastxlsx::CellValueKind::Formula &&
            original_formula.text_value() == "A9+A9+A9+A9+A9",
        "failed insert_rows memory-budget mutation should preserve the original formula");
    check(!sheet.try_cell("A3").has_value(),
        "failed insert_rows memory-budget mutation should not leave a shifted formula readable");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "save_as after failed insert_rows memory-budget mutation should copy source entries");
}

void test_public_worksheet_editor_options_guard_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-options-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-options-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 1;

    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data", options);
    }), "public WorksheetEditor should enforce max_cells while materializing source cells");
    check(!editor.has_pending_changes(),
        "failed public WorksheetEditor materialization should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "failed public WorksheetEditor materialization should not queue public edits");
    check(!editor.last_edit_error().has_value(),
        "failed public WorksheetEditor materialization should not update last_edit_error");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after-options-failure")}});
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "after-options-failure",
        "editor should remain usable after failed public WorksheetEditor materialization");
}

void test_public_worksheet_editor_memory_budget_guard_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-memory-options-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-memory-options-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditorOptions options;
    options.memory_budget_bytes = 1;

    bool failed = false;
    try {
        (void)editor.try_worksheet("Data", options);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "public WorksheetEditor should expose source-load memory budget diagnostics");
    }
    check(failed,
        "public WorksheetEditor should enforce memory_budget_bytes while materializing source cells");
    check(!editor.has_pending_changes(),
        "memory-budget materialization failure should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "memory-budget materialization failure should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "memory-budget materialization failure should not leave a partial materialized session");
    check(editor.pending_materialized_cell_count() == 0,
        "memory-budget materialization failure should not expose partial materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "memory-budget materialization failure should not expose partial materialized memory");
    check(!editor.last_edit_error().has_value(),
        "memory-budget materialization failure should not update last_edit_error");

    std::optional<fastxlsx::WorksheetEditor> recovered = editor.try_worksheet("Data");
    check(recovered.has_value(),
        "editor should remain able to materialize the sheet after memory-budget failure");
    check(recovered.has_value() && recovered->cell_count() == 3,
        "recovered materialization should load all source cells after memory-budget failure");
    if (recovered.has_value()) {
        recovered->set_cell("A1", fastxlsx::CellValue::text("after-memory-budget-failure"));
    }
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "after-memory-budget-failure",
        "recovered WorksheetEditor session should save after memory-budget failure");
}

void test_public_worksheet_editor_mutation_memory_budget_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-mutation-memory-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-memory-output.xlsx");

    fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
    const std::size_t exact_memory_budget = sizing_sheet.estimated_memory_usage();

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditorOptions options;
    options.memory_budget_bytes = exact_memory_budget;
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    const std::size_t baseline_count = sheet.cell_count();
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    check(!sheet.try_cell("D4").has_value(),
        "mutation memory-budget test precondition should use a missing target cell");

    bool failed = false;
    try {
        sheet.set_cell("D4", fastxlsx::CellValue::text("mutation-memory-oversized"));
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "public WorksheetEditor mutation should expose memory budget diagnostics");
    }
    check(failed,
        "public WorksheetEditor should enforce memory_budget_bytes while mutating cells");
    check(editor.last_edit_error().has_value(),
        "failed memory-budget mutation should update last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "CellStore memory_budget_bytes guardrail exceeded",
            "last_edit_error should retain the mutation memory-budget diagnostic");
    }
    check(!sheet.has_pending_changes(),
        "failed memory-budget mutation should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "failed memory-budget mutation should not dirty the editor");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed memory-budget mutation should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "failed memory-budget mutation should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed memory-budget mutation should not expose dirty materialized memory");
    check(sheet.cell_count() == baseline_count,
        "failed memory-budget mutation should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        "failed memory-budget mutation should preserve sparse memory estimate");
    check(!sheet.try_cell("D4").has_value(),
        "failed memory-budget mutation should not leave the rejected cell readable");

    sheet.set_cell("A1", fastxlsx::CellValue::text("tiny"));
    check(!editor.last_edit_error().has_value(),
        "successful in-budget mutation should clear last_edit_error");
    check(sheet.has_pending_changes(),
        "successful in-budget mutation should dirty the materialized session");
    check(editor.has_pending_changes(),
        "successful in-budget mutation should dirty the editor");
    check(editor.pending_materialized_cell_count() == baseline_count,
        "successful overwrite should keep the sparse cell count stable");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "tiny",
        "successful in-budget mutation should persist through save_as");
    check_not_contains(worksheet_xml, "mutation-memory-oversized",
        "rejected memory-budget mutation should not leak into saved output");
}

void test_public_worksheet_editor_mutation_max_cells_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-mutation-max-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-max-cells-output.xlsx");

    fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
    const std::size_t exact_max_cells = sizing_sheet.cell_count();

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = exact_max_cells;
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    const std::size_t baseline_count = sheet.cell_count();
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    check(!sheet.try_cell("D4").has_value(),
        "mutation max-cells test precondition should use a missing target cell");

    bool failed = false;
    try {
        sheet.set_cell("D4", fastxlsx::CellValue::text("mutation-max-cells-rejected"));
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "CellStore max_cells guardrail exceeded",
            "public WorksheetEditor mutation should expose max_cells diagnostics");
    }
    check(failed, "public WorksheetEditor should enforce max_cells while mutating cells");
    check(editor.last_edit_error().has_value(),
        "failed max_cells mutation should update last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "CellStore max_cells guardrail exceeded",
            "last_edit_error should retain the mutation max_cells diagnostic");
    }
    check(!sheet.has_pending_changes(),
        "failed max_cells mutation should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "failed max_cells mutation should not dirty the editor");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed max_cells mutation should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "failed max_cells mutation should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed max_cells mutation should not expose dirty materialized memory");
    check(sheet.cell_count() == baseline_count,
        "failed max_cells mutation should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        "failed max_cells mutation should preserve sparse memory estimate");
    check(!sheet.try_cell("D4").has_value(),
        "failed max_cells mutation should not leave the rejected cell readable");

    sheet.set_cell("A1", fastxlsx::CellValue::text("after-max-cells-overwrite"));
    check(!editor.last_edit_error().has_value(),
        "successful overwrite under max_cells should clear last_edit_error");
    check(sheet.has_pending_changes(),
        "successful overwrite under max_cells should dirty the materialized session");
    check(editor.has_pending_changes(),
        "successful overwrite under max_cells should dirty the editor");
    check(editor.pending_materialized_cell_count() == baseline_count,
        "successful overwrite under max_cells should keep the sparse cell count stable");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "after-max-cells-overwrite",
        "successful overwrite under max_cells should persist through save_as");
    check_not_contains(worksheet_xml, "mutation-max-cells-rejected",
        "rejected max_cells mutation should not leak into saved output");
}

void test_public_worksheet_editor_erase_releases_guardrail_budget_for_insertions()
{
    const std::filesystem::path max_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-erase-max-budget-source.xlsx");
    const std::filesystem::path max_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-max-budget-output.xlsx");

    fastxlsx::WorkbookEditor max_sizing_editor = fastxlsx::WorkbookEditor::open(max_source);
    const fastxlsx::WorksheetEditor max_sizing_sheet = max_sizing_editor.worksheet("Data");
    const std::size_t exact_max_cells = max_sizing_sheet.cell_count();

    fastxlsx::WorkbookEditor max_editor = fastxlsx::WorkbookEditor::open(max_source);
    fastxlsx::WorksheetEditorOptions max_options;
    max_options.max_cells = exact_max_cells;
    fastxlsx::WorksheetEditor max_sheet = max_editor.worksheet("Data", max_options);

    const std::size_t max_baseline_count = max_sheet.cell_count();
    const std::size_t max_baseline_memory = max_sheet.estimated_memory_usage();

    bool max_insert_failed = false;
    try {
        max_sheet.set_cell("D4", fastxlsx::CellValue::text("max-before-erase"));
    } catch (const fastxlsx::FastXlsxError& error) {
        max_insert_failed = true;
        check_contains(error.what(), "CellStore max_cells guardrail exceeded",
            "exact max_cells should reject a new sparse record before erase");
    }
    check(max_insert_failed, "exact max_cells should reject insertion before erase");
    check(max_editor.last_edit_error().has_value(),
        "failed exact max_cells insertion should update last_edit_error before erase");
    check(!max_editor.has_pending_changes(),
        "failed exact max_cells insertion before erase should not dirty the editor");

    max_sheet.erase_cell("A2");
    check(!max_editor.last_edit_error().has_value(),
        "successful erase should clear the prior max_cells diagnostic");
    check(max_sheet.has_pending_changes(), "erasing an existing cell should dirty the session");
    check(max_editor.has_pending_changes(), "erasing an existing cell should dirty the editor");
    check(max_sheet.cell_count() == max_baseline_count - 1,
        "erase should release one sparse record from max_cells accounting");
    check(max_sheet.estimated_memory_usage() < max_baseline_memory,
        "erase should lower the sparse memory estimate before reinsertion");
    check(max_editor.pending_materialized_cell_count() == max_baseline_count - 1,
        "pending materialized cell count should reflect erase before reinsertion");
    check(max_editor.estimated_pending_materialized_memory_usage() ==
            max_sheet.estimated_memory_usage(),
        "pending materialized memory should reflect erase before reinsertion");
    check(max_sheet.try_cell("A2") == std::nullopt,
        "erase should remove the source-backed A2 record before reinsertion");

    max_sheet.set_cell("D4", fastxlsx::CellValue::text("after-erase-max-cells"));
    const fastxlsx::CellValue max_d4 = max_sheet.get_cell("D4");
    check(max_d4.kind() == fastxlsx::CellValueKind::Text &&
            max_d4.text_value() == "after-erase-max-cells",
        "erased max_cells budget should allow a replacement insertion");
    check(max_sheet.cell_count() == max_baseline_count,
        "replacement insertion after erase should restore the original sparse count");
    check(max_editor.pending_materialized_cell_count() == max_baseline_count,
        "pending materialized count should include the replacement insertion");

    max_editor.save_as(max_output);
    const auto max_output_entries = fastxlsx::test::read_zip_entries(max_output);
    const std::string max_worksheet_xml = max_output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(max_worksheet_xml, "after-erase-max-cells",
        "replacement insertion after max_cells erase should persist through save_as");
    check_not_contains(max_worksheet_xml, "max-before-erase",
        "rejected max_cells insertion before erase should not leak into output");
    check_not_contains(max_worksheet_xml, "placeholder-a2",
        "erased source-backed A2 should not leak into max_cells output");

    const std::filesystem::path memory_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-erase-memory-budget-source.xlsx");
    const std::filesystem::path memory_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-memory-budget-output.xlsx");

    fastxlsx::WorkbookEditor memory_sizing_editor =
        fastxlsx::WorkbookEditor::open(memory_source);
    const fastxlsx::WorksheetEditor memory_sizing_sheet =
        memory_sizing_editor.worksheet("Data");
    const std::size_t exact_memory_budget = memory_sizing_sheet.estimated_memory_usage();

    fastxlsx::WorkbookEditor memory_editor = fastxlsx::WorkbookEditor::open(memory_source);
    fastxlsx::WorksheetEditorOptions memory_options;
    memory_options.memory_budget_bytes = exact_memory_budget;
    fastxlsx::WorksheetEditor memory_sheet =
        memory_editor.worksheet("Data", memory_options);

    const std::size_t memory_baseline_count = memory_sheet.cell_count();
    const std::size_t memory_baseline_usage = memory_sheet.estimated_memory_usage();

    bool memory_insert_failed = false;
    try {
        memory_sheet.set_cell("D4", fastxlsx::CellValue::text("memory-before-erase"));
    } catch (const fastxlsx::FastXlsxError& error) {
        memory_insert_failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "exact memory budget should reject a new sparse record before erase");
    }
    check(memory_insert_failed,
        "exact memory budget should reject insertion before erase");
    check(memory_editor.last_edit_error().has_value(),
        "failed exact memory-budget insertion should update last_edit_error before erase");
    check(!memory_editor.has_pending_changes(),
        "failed exact memory-budget insertion before erase should not dirty the editor");

    memory_sheet.erase_cell("A2");
    check(!memory_editor.last_edit_error().has_value(),
        "successful erase should clear the prior memory-budget diagnostic");
    check(memory_sheet.cell_count() == memory_baseline_count - 1,
        "erase should release one sparse record from memory-budget accounting");
    check(memory_sheet.estimated_memory_usage() < memory_baseline_usage,
        "erase should lower the sparse memory estimate for the memory-budget path");

    memory_sheet.set_cell("D4", fastxlsx::CellValue::text("mem-ok"));
    const fastxlsx::CellValue memory_d4 = memory_sheet.get_cell("D4");
    check(memory_d4.kind() == fastxlsx::CellValueKind::Text &&
            memory_d4.text_value() == "mem-ok",
        "erased memory budget should allow a smaller replacement insertion");
    check(memory_sheet.cell_count() == memory_baseline_count,
        "memory-budget replacement insertion after erase should restore sparse count");
    check(memory_sheet.estimated_memory_usage() <= exact_memory_budget,
        "memory-budget replacement insertion after erase should stay within budget");
    check(memory_editor.pending_materialized_cell_count() == memory_baseline_count,
        "pending materialized count should include the memory-budget replacement");

    memory_editor.save_as(memory_output);
    const auto memory_output_entries = fastxlsx::test::read_zip_entries(memory_output);
    const std::string memory_worksheet_xml =
        memory_output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(memory_worksheet_xml, "mem-ok",
        "replacement insertion after memory-budget erase should persist through save_as");
    check_not_contains(memory_worksheet_xml, "memory-before-erase",
        "rejected memory-budget insertion before erase should not leak into output");
    check_not_contains(memory_worksheet_xml, "placeholder-a2",
        "erased source-backed A2 should not leak into memory-budget output");
}

void test_public_worksheet_editor_missing_erase_after_guardrail_failure_stays_clean()
{
    const std::filesystem::path max_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-missing-erase-max-source.xlsx");
    const std::filesystem::path max_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-missing-erase-max-output.xlsx");

    fastxlsx::WorkbookEditor max_sizing_editor = fastxlsx::WorkbookEditor::open(max_source);
    const fastxlsx::WorksheetEditor max_sizing_sheet =
        max_sizing_editor.worksheet("Data");
    const std::size_t exact_max_cells = max_sizing_sheet.cell_count();

    fastxlsx::WorkbookEditor max_editor = fastxlsx::WorkbookEditor::open(max_source);
    fastxlsx::WorksheetEditorOptions max_options;
    max_options.max_cells = exact_max_cells;
    fastxlsx::WorksheetEditor max_sheet =
        max_editor.worksheet("Data", max_options);

    const std::size_t max_baseline_count = max_sheet.cell_count();
    const std::size_t max_baseline_memory = max_sheet.estimated_memory_usage();
    check(!max_sheet.try_cell("D4").has_value(),
        "missing-erase max_cells test precondition should use a missing target cell");

    bool max_insert_failed = false;
    try {
        max_sheet.set_cell("D4", fastxlsx::CellValue::text("missing-erase-max-rejected"));
    } catch (const fastxlsx::FastXlsxError& error) {
        max_insert_failed = true;
        check_contains(error.what(), "CellStore max_cells guardrail exceeded",
            "exact max_cells should reject the missing-erase setup insertion");
    }
    check(max_insert_failed,
        "exact max_cells should reject the missing-erase setup insertion");
    check(max_editor.last_edit_error().has_value(),
        "failed max_cells insertion should seed last_edit_error before missing erase");
    check(!max_editor.has_pending_changes(),
        "failed max_cells insertion before missing erase should keep the editor clean");

    max_sheet.erase_cell("D4");
    check(!max_editor.last_edit_error().has_value(),
        "missing erase should clear the prior max_cells diagnostic");
    check(!max_sheet.has_pending_changes(),
        "erasing the still-missing max_cells target should keep the session clean");
    check(!max_editor.has_pending_changes(),
        "erasing the still-missing max_cells target should keep the editor clean");
    check(max_editor.pending_materialized_worksheet_names().empty(),
        "missing max_cells erase should not expose dirty materialized names");
    check(max_editor.pending_materialized_cell_count() == 0,
        "missing max_cells erase should not expose dirty materialized cells");
    check(max_editor.estimated_pending_materialized_memory_usage() == 0,
        "missing max_cells erase should not expose dirty materialized memory");
    check(max_sheet.cell_count() == max_baseline_count,
        "missing max_cells erase should not change sparse cell count");
    check(max_sheet.estimated_memory_usage() == max_baseline_memory,
        "missing max_cells erase should not change sparse memory estimate");
    check(!max_sheet.try_cell("D4").has_value(),
        "missing max_cells erase should keep the rejected target absent");

    max_editor.save_as(max_output);
    const auto max_output_entries = fastxlsx::test::read_zip_entries(max_output);
    const std::string max_worksheet_xml =
        max_output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(max_worksheet_xml, "placeholder-a2",
        "clean save after missing max_cells erase should preserve source A2");
    check_not_contains(max_worksheet_xml, "missing-erase-max-rejected",
        "rejected max_cells text should not leak after missing erase");

    const std::filesystem::path memory_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-missing-erase-memory-source.xlsx");
    const std::filesystem::path memory_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-missing-erase-memory-output.xlsx");

    fastxlsx::WorkbookEditor memory_sizing_editor =
        fastxlsx::WorkbookEditor::open(memory_source);
    const fastxlsx::WorksheetEditor memory_sizing_sheet =
        memory_sizing_editor.worksheet("Data");
    const std::size_t exact_memory_budget =
        memory_sizing_sheet.estimated_memory_usage();

    fastxlsx::WorkbookEditor memory_editor =
        fastxlsx::WorkbookEditor::open(memory_source);
    fastxlsx::WorksheetEditorOptions memory_options;
    memory_options.memory_budget_bytes = exact_memory_budget;
    fastxlsx::WorksheetEditor memory_sheet =
        memory_editor.worksheet("Data", memory_options);

    const std::size_t memory_baseline_count = memory_sheet.cell_count();
    const std::size_t memory_baseline_usage =
        memory_sheet.estimated_memory_usage();
    check(!memory_sheet.try_cell("D4").has_value(),
        "missing-erase memory-budget test precondition should use a missing target cell");

    bool memory_insert_failed = false;
    try {
        memory_sheet.set_cell("D4",
            fastxlsx::CellValue::text("missing-erase-memory-rejected"));
    } catch (const fastxlsx::FastXlsxError& error) {
        memory_insert_failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "exact memory budget should reject the missing-erase setup insertion");
    }
    check(memory_insert_failed,
        "exact memory budget should reject the missing-erase setup insertion");
    check(memory_editor.last_edit_error().has_value(),
        "failed memory-budget insertion should seed last_edit_error before missing erase");
    check(!memory_editor.has_pending_changes(),
        "failed memory-budget insertion before missing erase should keep the editor clean");

    memory_sheet.erase_cell("D4");
    check(!memory_editor.last_edit_error().has_value(),
        "missing erase should clear the prior memory-budget diagnostic");
    check(!memory_sheet.has_pending_changes(),
        "erasing the still-missing memory-budget target should keep the session clean");
    check(!memory_editor.has_pending_changes(),
        "erasing the still-missing memory-budget target should keep the editor clean");
    check(memory_editor.pending_materialized_worksheet_names().empty(),
        "missing memory-budget erase should not expose dirty materialized names");
    check(memory_editor.pending_materialized_cell_count() == 0,
        "missing memory-budget erase should not expose dirty materialized cells");
    check(memory_editor.estimated_pending_materialized_memory_usage() == 0,
        "missing memory-budget erase should not expose dirty materialized memory");
    check(memory_sheet.cell_count() == memory_baseline_count,
        "missing memory-budget erase should not change sparse cell count");
    check(memory_sheet.estimated_memory_usage() == memory_baseline_usage,
        "missing memory-budget erase should not change sparse memory estimate");
    check(!memory_sheet.try_cell("D4").has_value(),
        "missing memory-budget erase should keep the rejected target absent");

    memory_editor.save_as(memory_output);
    const auto memory_output_entries =
        fastxlsx::test::read_zip_entries(memory_output);
    const std::string memory_worksheet_xml =
        memory_output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(memory_worksheet_xml, "placeholder-a2",
        "clean save after missing memory-budget erase should preserve source A2");
    check_not_contains(memory_worksheet_xml, "missing-erase-memory-rejected",
        "rejected memory-budget text should not leak after missing erase");
}

void test_public_worksheet_editor_blank_insertions_obey_guardrail_budgets()
{
    const std::filesystem::path max_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-blank-max-guard-source.xlsx");
    const std::filesystem::path max_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-max-guard-output.xlsx");

    fastxlsx::WorkbookEditor max_sizing_editor =
        fastxlsx::WorkbookEditor::open(max_source);
    const fastxlsx::WorksheetEditor max_sizing_sheet =
        max_sizing_editor.worksheet("Data");
    const std::size_t exact_max_cells = max_sizing_sheet.cell_count();

    fastxlsx::WorkbookEditor max_editor = fastxlsx::WorkbookEditor::open(max_source);
    fastxlsx::WorksheetEditorOptions max_options;
    max_options.max_cells = exact_max_cells;
    fastxlsx::WorksheetEditor max_sheet =
        max_editor.worksheet("Data", max_options);

    const std::size_t max_baseline_count = max_sheet.cell_count();
    const std::size_t max_baseline_memory = max_sheet.estimated_memory_usage();
    check(!max_sheet.try_cell("D4").has_value(),
        "blank max_cells guard test precondition should use a missing target cell");

    bool max_blank_failed = false;
    try {
        max_sheet.set_cell("D4", fastxlsx::CellValue::blank());
    } catch (const fastxlsx::FastXlsxError& error) {
        max_blank_failed = true;
        check_contains(error.what(), "CellStore max_cells guardrail exceeded",
            "explicit blank insertion should expose max_cells diagnostics");
    }
    check(max_blank_failed,
        "explicit blank insertion should obey the exact max_cells guardrail");
    check(max_editor.last_edit_error().has_value(),
        "failed explicit blank max_cells insertion should update last_edit_error");
    check(!max_sheet.has_pending_changes(),
        "failed explicit blank max_cells insertion should not dirty the session");
    check(!max_editor.has_pending_changes(),
        "failed explicit blank max_cells insertion should not dirty the editor");
    check(max_editor.pending_materialized_worksheet_names().empty(),
        "failed explicit blank max_cells insertion should not expose dirty names");
    check(max_editor.pending_materialized_cell_count() == 0,
        "failed explicit blank max_cells insertion should not expose dirty cell count");
    check(max_editor.estimated_pending_materialized_memory_usage() == 0,
        "failed explicit blank max_cells insertion should not expose dirty memory");
    check(max_sheet.cell_count() == max_baseline_count,
        "failed explicit blank max_cells insertion should preserve sparse count");
    check(max_sheet.estimated_memory_usage() == max_baseline_memory,
        "failed explicit blank max_cells insertion should preserve memory estimate");
    check(!max_sheet.try_cell("D4").has_value(),
        "failed explicit blank max_cells insertion should not create a blank D4");

    max_sheet.set_cell("A1", fastxlsx::CellValue::blank());
    check(!max_editor.last_edit_error().has_value(),
        "successful existing-cell blank overwrite should clear max_cells diagnostic");
    check(max_sheet.has_pending_changes(),
        "successful existing-cell blank overwrite should dirty the session");
    check(max_editor.pending_materialized_cell_count() == max_baseline_count,
        "existing-cell blank overwrite should keep the sparse count stable");
    check(max_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "existing-cell blank overwrite should read back as explicit blank");

    max_editor.save_as(max_output);
    const auto max_output_entries = fastxlsx::test::read_zip_entries(max_output);
    const std::string max_worksheet_xml =
        max_output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(max_worksheet_xml, R"(<c r="A1"/>)",
        "existing-cell blank overwrite should save as an empty A1 cell");
    check_not_contains(max_worksheet_xml, "placeholder-a1",
        "blank overwrite should remove the previous A1 text");
    check_not_contains(max_worksheet_xml, R"(r="D4")",
        "rejected blank max_cells insertion should not leak as D4");

    const std::filesystem::path memory_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-blank-memory-guard-source.xlsx");
    const std::filesystem::path memory_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-memory-guard-output.xlsx");

    fastxlsx::WorkbookEditor memory_sizing_editor =
        fastxlsx::WorkbookEditor::open(memory_source);
    const fastxlsx::WorksheetEditor memory_sizing_sheet =
        memory_sizing_editor.worksheet("Data");
    const std::size_t exact_memory_budget =
        memory_sizing_sheet.estimated_memory_usage();

    fastxlsx::WorkbookEditor memory_editor =
        fastxlsx::WorkbookEditor::open(memory_source);
    fastxlsx::WorksheetEditorOptions memory_options;
    memory_options.memory_budget_bytes = exact_memory_budget;
    fastxlsx::WorksheetEditor memory_sheet =
        memory_editor.worksheet("Data", memory_options);

    const std::size_t memory_baseline_count = memory_sheet.cell_count();
    const std::size_t memory_baseline_usage =
        memory_sheet.estimated_memory_usage();
    check(!memory_sheet.try_cell("D4").has_value(),
        "blank memory-budget guard test precondition should use a missing target cell");

    bool memory_blank_failed = false;
    try {
        memory_sheet.set_cell("D4", fastxlsx::CellValue::blank());
    } catch (const fastxlsx::FastXlsxError& error) {
        memory_blank_failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "explicit blank insertion should expose memory-budget diagnostics");
    }
    check(memory_blank_failed,
        "explicit blank insertion should obey the exact memory-budget guardrail");
    check(memory_editor.last_edit_error().has_value(),
        "failed explicit blank memory-budget insertion should update last_edit_error");
    check(!memory_sheet.has_pending_changes(),
        "failed explicit blank memory-budget insertion should not dirty the session");
    check(!memory_editor.has_pending_changes(),
        "failed explicit blank memory-budget insertion should not dirty the editor");
    check(memory_editor.pending_materialized_worksheet_names().empty(),
        "failed explicit blank memory-budget insertion should not expose dirty names");
    check(memory_editor.pending_materialized_cell_count() == 0,
        "failed explicit blank memory-budget insertion should not expose dirty cell count");
    check(memory_editor.estimated_pending_materialized_memory_usage() == 0,
        "failed explicit blank memory-budget insertion should not expose dirty memory");
    check(memory_sheet.cell_count() == memory_baseline_count,
        "failed explicit blank memory-budget insertion should preserve sparse count");
    check(memory_sheet.estimated_memory_usage() == memory_baseline_usage,
        "failed explicit blank memory-budget insertion should preserve memory estimate");
    check(!memory_sheet.try_cell("D4").has_value(),
        "failed explicit blank memory-budget insertion should not create a blank D4");

    memory_sheet.set_cell("A1", fastxlsx::CellValue::blank());
    check(!memory_editor.last_edit_error().has_value(),
        "successful existing-cell blank overwrite should clear memory-budget diagnostic");
    check(memory_sheet.has_pending_changes(),
        "successful existing-cell blank overwrite should dirty the memory-budget session");
    check(memory_sheet.cell_count() == memory_baseline_count,
        "existing-cell blank overwrite should keep memory-budget sparse count stable");
    check(memory_sheet.estimated_memory_usage() <= exact_memory_budget,
        "existing-cell blank overwrite should stay within exact memory budget");
    check(memory_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "memory-budget blank overwrite should read back as explicit blank");

    memory_editor.save_as(memory_output);
    const auto memory_output_entries =
        fastxlsx::test::read_zip_entries(memory_output);
    const std::string memory_worksheet_xml =
        memory_output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(memory_worksheet_xml, R"(<c r="A1"/>)",
        "memory-budget blank overwrite should save as an empty A1 cell");
    check_not_contains(memory_worksheet_xml, "placeholder-a1",
        "memory-budget blank overwrite should remove the previous A1 text");
    check_not_contains(memory_worksheet_xml, R"(r="D4")",
        "rejected blank memory-budget insertion should not leak as D4");
}

void test_public_worksheet_editor_last_edit_error_replaces_failed_mutation_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-last-error-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-last-error-replace-output.xlsx");

    fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
    const std::size_t exact_memory_budget = sizing_sheet.estimated_memory_usage();

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditorOptions options;
    options.memory_budget_bytes = exact_memory_budget;
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    const std::size_t baseline_count = sheet.cell_count();
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    check(!sheet.try_cell("D4").has_value(),
        "last-error replacement test precondition should use a missing D4 cell");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-reference-payload"));
    }), "invalid A1 mutation should seed last_edit_error");
    check(editor.last_edit_error().has_value(),
        "invalid A1 mutation should populate last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "WorksheetEditor cell reference is invalid",
            "invalid A1 diagnostic should be visible");
    }
    const std::optional<std::string> invalid_reference_error = editor.last_edit_error();
    check_public_inspection_preserves_last_edit_error(editor, invalid_reference_error);

    bool memory_failed = false;
    try {
        sheet.set_cell("D4", fastxlsx::CellValue::text("replacement-memory-diagnostic"));
    } catch (const fastxlsx::FastXlsxError& error) {
        memory_failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "memory guardrail failure should expose CellStore diagnostic");
    }
    check(memory_failed,
        "memory guardrail failure should replace invalid-reference diagnostic");
    check(editor.last_edit_error().has_value(),
        "memory guardrail failure should keep last_edit_error populated");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "CellStore memory_budget_bytes guardrail exceeded",
            "latest diagnostic should be the memory-budget failure");
        check_not_contains(*editor.last_edit_error(),
            "WorksheetEditor cell reference is invalid",
            "memory-budget failure should replace the old invalid-reference diagnostic");
    }
    const std::optional<std::string> memory_error = editor.last_edit_error();
    check_public_inspection_preserves_last_edit_error(editor, memory_error);

    bool coordinate_failed = false;
    try {
        sheet.erase_cell(1048577, 1);
    } catch (const fastxlsx::FastXlsxError& error) {
        coordinate_failed = true;
        check_contains(error.what(), "WorksheetEditor cell coordinate is invalid",
            "invalid coordinate erase should expose coordinate diagnostic");
    }
    check(coordinate_failed,
        "invalid coordinate erase should replace memory-budget diagnostic");
    check(editor.last_edit_error().has_value(),
        "invalid coordinate erase should keep last_edit_error populated");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "WorksheetEditor cell coordinate is invalid",
            "latest diagnostic should be the invalid coordinate failure");
        check_not_contains(*editor.last_edit_error(),
            "memory_budget_bytes guardrail exceeded",
            "coordinate failure should replace the old memory-budget diagnostic");
    }

    check(!sheet.has_pending_changes(),
        "replaced failure diagnostics should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "replaced failure diagnostics should not dirty the editor");
    check(editor.pending_materialized_worksheet_names().empty(),
        "replaced failure diagnostics should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "replaced failure diagnostics should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "replaced failure diagnostics should not expose dirty materialized memory");
    check(sheet.cell_count() == baseline_count,
        "replaced failure diagnostics should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        "replaced failure diagnostics should preserve sparse memory estimate");
    check(!sheet.try_cell("D4").has_value(),
        "replaced failure diagnostics should keep the rejected D4 cell absent");

    sheet.set_cell("A1", fastxlsx::CellValue::text("fixed"));
    check(!editor.last_edit_error().has_value(),
        "successful in-budget mutation should clear replaced failure diagnostic");
    check(sheet.has_pending_changes(),
        "successful in-budget mutation should dirty the materialized session");
    check(editor.has_pending_changes(),
        "successful in-budget mutation should dirty the editor");
    check(editor.pending_materialized_cell_count() == baseline_count,
        "successful overwrite after diagnostic replacement should keep sparse count stable");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "fixed",
        "successful mutation after diagnostic replacement should persist");
    check_not_contains(worksheet_xml, "invalid-reference-payload",
        "invalid-reference payload should not leak into output");
    check_not_contains(worksheet_xml, "replacement-memory-diagnostic",
        "memory-budget rejected payload should not leak into output");
    check_not_contains(worksheet_xml, R"(r="D4")",
        "memory-budget rejected D4 should not leak into output");
}

void test_public_workbook_editor_last_edit_error_replaces_mixed_edit_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-mixed-last-error-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-mixed-last-error-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    bool replacement_failed = false;
    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::text("missing-replacement-payload")}});
    } catch (const fastxlsx::FastXlsxError& error) {
        replacement_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "missing replacement should seed last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "replacement last_edit_error should match the thrown diagnostic");
            check_contains(*last_error, "Missing",
                "replacement diagnostic should mention the missing sheet");
            check_contains(*last_error, "current planned catalog",
                "replacement diagnostic should retain planned-catalog context");
        }
    }
    check(replacement_failed,
        "missing replacement should fail before mixed diagnostic replacement");
    const std::optional<std::string> replacement_error = editor.last_edit_error();
    check_public_inspection_preserves_last_edit_error(editor, replacement_error);

    bool rename_failed = false;
    try {
        editor.rename_sheet("Data", "Bad/Name");
    } catch (const fastxlsx::FastXlsxError& error) {
        rename_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "invalid rename should replace replacement last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "rename last_edit_error should match the thrown diagnostic");
            check_contains(*last_error, "Bad/Name",
                "rename diagnostic should mention the rejected sheet name");
            check_not_contains(*last_error, "Missing",
                "rename diagnostic should replace the missing replacement diagnostic");
        }
    }
    check(rename_failed,
        "invalid rename should fail during mixed diagnostic replacement");
    const std::optional<std::string> rename_error = editor.last_edit_error();
    check_public_inspection_preserves_last_edit_error(editor, rename_error);

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    bool mutation_failed = false;
    try {
        sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-mutation-payload"));
    } catch (const fastxlsx::FastXlsxError& error) {
        mutation_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "invalid WorksheetEditor mutation should replace rename last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "WorksheetEditor mutation last_edit_error should match the thrown diagnostic");
            check_contains(*last_error, "WorksheetEditor cell reference is invalid",
                "WorksheetEditor mutation diagnostic should mention the invalid reference");
            check_not_contains(*last_error, "Bad/Name",
                "WorksheetEditor mutation should replace the rename diagnostic");
            check_not_contains(*last_error, "Missing",
                "WorksheetEditor mutation should not retain the older replacement diagnostic");
        }
    }
    check(mutation_failed,
        "invalid WorksheetEditor mutation should fail during mixed diagnostic replacement");

    check(!sheet.has_pending_changes(),
        "mixed failed edits should leave the materialized session clean");
    check(!editor.has_pending_changes(),
        "mixed failed edits should leave the editor clean");
    check(editor.pending_change_count() == 0,
        "mixed failed edits should not add public pending changes");
    check(editor.pending_replacement_worksheet_names().empty(),
        "mixed failed edits should not leave pending replacements");
    check(editor.pending_materialized_worksheet_names().empty(),
        "mixed failed edits should not leave dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "mixed failed edits should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "mixed failed edits should not leave dirty materialized memory");

    editor.replace_sheet_data("Untouched",
        {{fastxlsx::CellValue::text("mixed-diagnostic-recovered")}});
    check(!editor.last_edit_error().has_value(),
        "successful public edit should clear mixed last_edit_error");
    check(editor.has_pending_changes(),
        "successful recovery replacement should dirty the editor");
    check(editor.pending_change_count() == 1,
        "successful recovery replacement should add one public pending change");
    check(editor.has_pending_replacement("Untouched"),
        "successful recovery replacement should be tracked under the target sheet");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean materialized Data session should not become dirty after other-sheet recovery");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_xml = output_entries.at("xl/worksheets/sheet2.xml");
    check_contains(data_xml, "placeholder-a1",
        "clean Data materialized session should remain copy-original after mixed failures");
    check_not_contains(data_xml, "invalid-mutation-payload",
        "invalid WorksheetEditor payload should not leak into Data output");
    check_contains(untouched_xml, "mixed-diagnostic-recovered",
        "successful recovery replacement should persist on Untouched");
    check_not_contains(untouched_xml, "missing-replacement-payload",
        "failed replacement payload should not leak into output");
    check_not_contains(output_entries.at("xl/workbook.xml"), "Bad/Name",
        "failed rename target should not leak into workbook catalog");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = workbook_editor_shard_from_args(argc, argv);
        std::printf("fastxlsx.workbook_editor shard: %.*s\n",
            static_cast<int>(shard.size()), shard.data());

        if (should_run_workbook_editor_shard(shard, "public-state")) {
            test_public_worksheet_editor_has_pending_changes_tracks_dirty_state();
            test_public_worksheet_editor_handle_remains_valid_after_save_as();
            test_public_workbook_editor_pending_materialized_names_track_dirty_state();
            test_public_workbook_editor_pending_materialized_names_move_with_owner();
            test_public_workbook_editor_pending_materialized_aggregate_diagnostics();
            test_public_workbook_editor_pending_materialized_aggregate_moves_with_owner();
            test_public_workbook_editor_pending_summaries_include_materialized_dirty_state();
            test_public_workbook_editor_pending_materialized_summaries_move_with_owner();
            test_public_worksheet_editor_get_cell_missing_and_blank_semantics();
            test_public_worksheet_editor_a1_overloads_read_mutate_and_save();
            test_public_worksheet_editor_a1_overloads_reject_invalid_references();
            test_public_worksheet_editor_a1_range_mutations_sparse_semantics();
            test_public_worksheet_editor_a1_range_mutations_invalid_references();
            test_public_worksheet_editor_row_column_overloads_reject_invalid_coordinates();
            test_public_worksheet_editor_invalid_cell_reads_preserve_prior_diagnostic();
            test_public_worksheet_editor_sparse_cells_snapshot();
            test_public_worksheet_editor_sparse_cells_range_snapshot();
            test_public_worksheet_editor_sparse_cells_a1_range_snapshot();
            test_public_worksheet_editor_sparse_cells_coordinate_batch_snapshot();
            test_public_worksheet_editor_used_range_tracks_sparse_bounds();
            test_public_worksheet_editor_contains_cell_tracks_represented_state();
            test_public_worksheet_editor_row_and_column_cells_snapshot();
            test_public_worksheet_editor_row_and_column_cells_invalid_reads_preserve_diagnostics();
            test_public_worksheet_editor_sparse_cells_invalid_range_preserves_prior_diagnostic();
            test_public_worksheet_editor_erase_cell_auto_flushes_on_save_as();
            test_public_worksheet_editor_erase_cells_range_reacquires_saved_state();
            test_public_worksheet_editor_initializer_list_batch_overloads();
            test_public_worksheet_editor_append_row_appends_after_sparse_max_row();
            test_public_worksheet_editor_append_row_noop_and_guardrails();
            test_public_worksheet_editor_set_row_replaces_sparse_row();
            test_public_worksheet_editor_set_row_empty_and_guardrails();
            test_public_worksheet_editor_set_column_replaces_sparse_column();
            test_public_worksheet_editor_set_column_empty_and_guardrails();
            test_public_worksheet_editor_set_row_values_preserves_styles_and_tail();
            test_public_worksheet_editor_set_column_values_noop_invalid_and_budget();
            test_public_worksheet_editor_clear_and_erase_all_cells();
            test_public_worksheet_editor_clear_row_preserves_sparse_records();
            test_public_worksheet_editor_clear_columns_noop_invalid_and_range();
            test_public_worksheet_editor_erase_row_removes_sparse_row();
            test_public_worksheet_editor_erase_rows_noop_invalid_and_range();
            test_public_worksheet_editor_erase_column_removes_sparse_column();
            test_public_worksheet_editor_erase_columns_noop_invalid_and_range();
            test_public_worksheet_editor_insert_rows_shifts_sparse_records();
            test_public_worksheet_editor_insert_rows_shifted_sparse_snapshot();
            test_public_worksheet_editor_delete_rows_shifts_sparse_records();
            test_public_worksheet_editor_insert_columns_shifts_sparse_records();
            test_public_worksheet_editor_delete_columns_shifts_sparse_records();
            test_public_worksheet_editor_shift_formula_out_of_bounds_references();
            test_public_worksheet_editor_row_column_shift_noop_and_invalid_preserve_state();
            test_public_worksheet_editor_shift_memory_guard_failure_preserves_state();
            test_public_worksheet_editor_options_guard_failure_preserves_state();
            test_public_worksheet_editor_memory_budget_guard_failure_preserves_state();
            test_public_worksheet_editor_mutation_memory_budget_failure_preserves_state();
            test_public_worksheet_editor_mutation_max_cells_failure_preserves_state();
            test_public_worksheet_editor_erase_releases_guardrail_budget_for_insertions();
            test_public_worksheet_editor_missing_erase_after_guardrail_failure_stays_clean();
            test_public_worksheet_editor_blank_insertions_obey_guardrail_budgets();
            test_public_worksheet_editor_last_edit_error_replaces_failed_mutation_diagnostics();
            test_public_workbook_editor_last_edit_error_replaces_mixed_edit_diagnostics();
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor tests passed\n");
    return 0;
}
