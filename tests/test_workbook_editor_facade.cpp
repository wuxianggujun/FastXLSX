// Structure tests for the public Patch-mode WorkbookEditor facade.
// Split from test_workbook_editor.cpp to keep the monolithic WorkbookEditor
// test source focused on source/materialized worksheet coverage.

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
    return shard == "all" || shard == "core" || shard == "public"
        || shard == "public-edge"
        || shard == "source-success" || shard == "source-failure"
        || shard == "source-failure-core" || shard == "source-failure-shapes"
        || shard == "materialized" || shard == "facade";
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

void test_pending_change_diagnostics_track_public_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-pending-source.xlsx");

    fastxlsx::WorkbookEditor clean_editor = fastxlsx::WorkbookEditor::open(source);
    check_workbook_editor_public_clean_state(clean_editor, "newly opened editor");
    check(!clean_editor.has_pending_replacement("Data"),
        "newly opened editor should report no pending replacement for Data");

    check(threw_fastxlsx_error([&] {
        clean_editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    }), "rejected replace_sheet_data should throw FastXlsxError");
    check_workbook_editor_public_no_pending_state(
        clean_editor, "rejected replace_sheet_data");
    check(clean_editor.pending_replacement_worksheet_names().empty(),
        "rejected replace_sheet_data should not add pending replacement names");

    check(threw_fastxlsx_error([&] { clean_editor.rename_sheet("Data", "Bad/Name"); }),
        "rejected rename_sheet should throw FastXlsxError");
    check_workbook_editor_public_no_pending_state(
        clean_editor, "rejected rename_sheet");

    clean_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
    check(clean_editor.has_pending_changes(),
        "successful replace_sheet_data should mark the editor dirty");
    check(clean_editor.pending_change_count() > 0,
        "successful replace_sheet_data should expose a coarse pending count");
    check(clean_editor.pending_replacement_cell_count() == 1,
        "successful replace_sheet_data should expose final queued replacement cells");
    {
        const std::vector<std::string> pending_names =
            clean_editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "Data",
            "successful replace_sheet_data should expose the pending replacement sheet");
        check(clean_editor.has_pending_replacement("Data"),
            "successful replace_sheet_data should mark Data as pending replacement");
        check(!clean_editor.has_pending_replacement("Untouched"),
            "successful replace_sheet_data should not mark untouched sheets");
    }
    check(clean_editor.estimated_pending_replacement_memory_usage() > 0,
        "successful replace_sheet_data should expose estimated replacement memory");

    fastxlsx::WorkbookEditor rename_editor = fastxlsx::WorkbookEditor::open(source);
    rename_editor.rename_sheet("Data", "Renamed");
    check(rename_editor.has_pending_changes(),
        "successful rename_sheet should mark the editor dirty");
    check(rename_editor.pending_change_count() > 0,
        "successful rename_sheet should expose a coarse pending count");
    check_workbook_editor_no_replacement_diagnostics(
        rename_editor, "successful rename_sheet");
    check(!rename_editor.has_pending_replacement("Renamed"),
        "rename_sheet should not mark the renamed sheet as data-replaced");
}

void test_last_edit_error_tracks_failed_public_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-last-error-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(!editor.last_edit_error().has_value(),
        "newly opened editor should report no last edit error");

    bool failed = false;
    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "failed replace_sheet_data should record last edit error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "last edit error should match the thrown replace_sheet_data diagnostic");
            check_contains(*last_error, "current planned catalog",
                "last edit error should preserve public planned-catalog context");
        }
    }
    check(failed, "missing sheet replacement should fail");

    check_workbook_editor_public_no_pending_state(
        editor, "failed replace_sheet_data");
    check_public_inspection_preserves_last_edit_error(
        editor, editor.last_edit_error());

    editor.rename_sheet("Data", "Report");
    check(!editor.last_edit_error().has_value(),
        "successful rename_sheet should clear last edit error");

    failed = false;
    try {
        editor.rename_sheet("Report", "Bad/Name");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "failed rename_sheet should record last edit error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "last edit error should match the thrown rename_sheet diagnostic");
            check_contains(*last_error, "Bad/Name",
                "last edit error should include the rejected sheet name");
        }
    }
    check(failed, "invalid rename should fail");

    check_public_inspection_preserves_last_edit_error(
        editor, editor.last_edit_error());

    editor.replace_sheet_data("Report", {{fastxlsx::CellValue::number(2.0)}});
    check(!editor.last_edit_error().has_value(),
        "successful replace_sheet_data should clear last edit error");
}

void test_pending_worksheet_edit_summaries_track_public_facade_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-edit-summary-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.pending_worksheet_edits().empty(),
        "newly opened editor should report no pending worksheet edit summaries");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)}});
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "single replacement should report one worksheet edit summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "replacement summary should report the source sheet name");
            check(summary.planned_name == "Data",
                "replacement summary should report the current planned sheet name");
            check(!summary.renamed,
                "replacement-only summary should not be marked as renamed");
            check(summary.sheet_data_replaced,
                "replacement-only summary should report sheetData replacement");
            check(summary.replacement_cell_count == 2,
                "replacement summary should report final queued cell count");
            check(summary.estimated_replacement_memory_usage > 0,
                "replacement summary should report estimated replacement memory");
        }
    }

    editor.rename_sheet("Data", "Report");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "replace+rename should still report one worksheet edit summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "replace+rename summary should keep the source sheet name");
            check(summary.planned_name == "Report",
                "replace+rename summary should report the planned sheet name");
            check(summary.renamed,
                "replace+rename summary should be marked as renamed");
            check(summary.sheet_data_replaced,
                "replace+rename summary should report sheetData replacement");
            check(summary.replacement_cell_count == 2,
                "replace+rename summary should preserve queued replacement cells");
            check(summary.estimated_replacement_memory_usage > 0,
                "replace+rename summary should preserve replacement memory");
        }
    }

    editor.replace_sheet_data("Report",
        {{fastxlsx::CellValue::number(3.0), fastxlsx::CellValue::number(4.0)},
            {fastxlsx::CellValue::number(5.0)}});
    editor.rename_sheet("Untouched", "Archive");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 2,
            "replacement+rename and rename-only edits should report two summaries");
        if (summaries.size() == 2) {
            const auto& replaced = summaries[0];
            check(replaced.source_name == "Data",
                "summaries should follow source workbook sheet order");
            check(replaced.planned_name == "Report",
                "updated replacement summary should keep the planned name");
            check(replaced.renamed,
                "updated replacement summary should stay marked as renamed");
            check(replaced.sheet_data_replaced,
                "updated replacement summary should report replacement");
            check(replaced.replacement_cell_count == 3,
                "updated replacement summary should report final replacement cells");
            check(replaced.estimated_replacement_memory_usage > 0,
                "updated replacement summary should keep replacement memory");

            const auto& renamed_only = summaries[1];
            check(renamed_only.source_name == "Untouched",
                "rename-only summary should report the source sheet name");
            check(renamed_only.planned_name == "Archive",
                "rename-only summary should report the planned sheet name");
            check(renamed_only.renamed,
                "rename-only summary should be marked as renamed");
            check(!renamed_only.sheet_data_replaced,
                "rename-only summary should not report sheetData replacement");
            check(renamed_only.replacement_cell_count == 0,
                "rename-only summary should report zero replacement cells");
            check(renamed_only.estimated_replacement_memory_usage == 0,
                "rename-only summary should report zero replacement memory");
        }
    }

    editor.replace_sheet_data("Archive", {{fastxlsx::CellValue::text("archived")}});
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 2,
            "two replaced sheets should report two pending replacement names");
        if (pending_names.size() == 2) {
            check(pending_names[0] == "Report",
                "pending replacement names should follow current planned catalog order");
            check(pending_names[1] == "Archive",
                "pending replacement names should include renamed second sheet in order");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 2,
            "two replaced renamed sheets should still report two summaries");
        if (summaries.size() == 2) {
            check(summaries[0].source_name == "Data" && summaries[0].planned_name == "Report",
                "first summary should keep source-order Data -> Report mapping");
            check(summaries[0].sheet_data_replaced,
                "first summary should remain marked as replaced");
            check(summaries[1].source_name == "Untouched" &&
                    summaries[1].planned_name == "Archive",
                "second summary should keep source-order Untouched -> Archive mapping");
            check(summaries[1].renamed,
                "second summary should remain marked as renamed");
            check(summaries[1].sheet_data_replaced,
                "second summary should now report replacement after planned-name edit");
            check(summaries[1].replacement_cell_count == 1,
                "second summary should report its final replacement cell count");
            check(summaries[1].estimated_replacement_memory_usage > 0,
                "second summary should report its replacement memory estimate");
        }
    }
}

void test_replace_sheet_data_source_read_failure_preserves_public_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-source-read-failure.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-source-read-failure-output.xlsx");

    const std::string original_source_bytes = fastxlsx::test::read_file(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    std::string corrupted_source_bytes = original_source_bytes;
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
    corrupt_zip_entry_crc_metadata(corrupted_source_bytes, "xl/worksheets/sheet1.xml");
#else
    corrupt_zip_entry_payload(corrupted_source_bytes, "xl/worksheets/sheet1.xml");
#endif
    write_binary_file(source, corrupted_source_bytes);

    bool failed = false;
    try {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(5.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_contains(message,
            "current worksheet input for worksheet sheetData replacement output",
            "WorkbookEditor should preserve the internal sheetData current-input boundary");
        check_contains(message, "xl/worksheets/sheet1.xml",
            "WorkbookEditor should preserve the corrupt worksheet entry context");
        check_not_contains(message, "sheetData replacement XML",
            "WorkbookEditor should not mislabel source-entry failures as "
            "replacement payload failures");
#ifndef FASTXLSX_TEST_HAS_MINIZIP_NG
        check_contains(message, "CRC mismatch",
            "WorkbookEditor stored source read failure should preserve the ZIP CRC error");
        check_contains(message, "expected ",
            "WorkbookEditor stored CRC failure should report the expected CRC");
        check_contains(message, "actual ",
            "WorkbookEditor stored CRC failure should report the actual CRC");
#else
        if (message.find("CRC mismatch") != std::string::npos) {
            check_contains(message, "expected ",
                "WorkbookEditor CRC failure should report the expected CRC");
            check_contains(message, "actual ",
                "WorkbookEditor CRC failure should report the actual CRC");
        }
#endif
    }

    check(failed,
        "replace_sheet_data should fail when the source worksheet entry cannot be read");
    check_workbook_editor_public_no_pending_state(editor, "source read failure");
    check_workbook_editor_no_replacement_payload_size_diagnostics(
        editor, "source read failure");
    check(editor.has_worksheet("Data"),
        "source read failure should not disturb the opened source sheet catalog");

    write_binary_file(source, original_source_bytes);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(6.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<v>6</v>)",
        "editor should remain usable after a source read failure once the source is restored");
}

void check_clean_replace_sheet_data_failure_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    check_workbook_editor_public_no_pending_state(editor, scenario);
    check_workbook_editor_no_replacement_payload_size_diagnostics(editor, scenario);
    check(editor.has_worksheet("Data"),
        std::string(scenario) + " should not disturb the opened source sheet catalog");
}

void check_sheet_data_current_input_facade_error(
    const std::string& message, std::string_view scenario)
{
    check_contains(message,
        "current worksheet input for worksheet sheetData replacement output",
        std::string(scenario)
            + " should preserve the internal sheetData current-input boundary");
    check_not_contains(message, "sheetData replacement XML",
        std::string(scenario)
            + " should not be mislabeled as replacement payload input");
}

void test_replace_sheet_data_source_xml_failures_preserve_public_state()
{
    const std::filesystem::path missing_source =
        write_two_sheet_source("fastxlsx-workbook-editor-missing-source-sheetdata.xlsx");
    const std::filesystem::path missing_output =
        artifact("fastxlsx-workbook-editor-missing-source-sheetdata-output.xlsx");
    rewrite_package_entry_as_stored(missing_source, "xl/worksheets/sheet1.xml",
        R"(<worksheet><dimension ref="A1"/></worksheet>)");

    fastxlsx::WorkbookEditor missing_editor = fastxlsx::WorkbookEditor::open(missing_source);
    bool failed = false;
    try {
        missing_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_sheet_data_current_input_facade_error(
            message, "missing source sheetData failure");
        check_contains(message, "worksheet XML sheetData element is missing",
            "missing source sheetData failure should explain the missing target");
    }
    check(failed,
        "WorkbookEditor should reject replace_sheet_data when source worksheet has no sheetData");
    check_clean_replace_sheet_data_failure_state(
        missing_editor, "missing source sheetData failure");
    missing_editor.rename_sheet("Data", "MissingChecked");
    missing_editor.save_as(missing_output);
    check_contains(fastxlsx::test::read_zip_entries(missing_output).at("xl/workbook.xml"),
        R"(name="MissingChecked")",
        "editor should remain usable for a valid catalog edit after missing source sheetData");

    const std::filesystem::path malformed_source =
        write_two_sheet_source("fastxlsx-workbook-editor-malformed-source-sheetdata.xlsx");
    const std::filesystem::path malformed_output =
        artifact("fastxlsx-workbook-editor-malformed-source-sheetdata-output.xlsx");
    rewrite_package_entry_as_stored(malformed_source, "xl/worksheets/sheet1.xml",
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row>)"
        R"(<autoFilter ref="A1:A1"/>)"
        R"(</worksheet>)");

    fastxlsx::WorkbookEditor malformed_editor =
        fastxlsx::WorkbookEditor::open(malformed_source);
    failed = false;
    try {
        malformed_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(8.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_sheet_data_current_input_facade_error(
            message, "malformed source sheetData failure");
        check_contains(message, "worksheet event reader found an invalid worksheet boundary",
            "malformed source sheetData failure should preserve event-reader diagnostics");
    }
    check(failed,
        "WorkbookEditor should reject replace_sheet_data when source sheetData is malformed");
    check_clean_replace_sheet_data_failure_state(
        malformed_editor, "malformed source sheetData failure");
    malformed_editor.rename_sheet("Data", "MalformedChecked");
    malformed_editor.save_as(malformed_output);
    check_contains(fastxlsx::test::read_zip_entries(malformed_output).at("xl/workbook.xml"),
        R"(name="MalformedChecked")",
        "editor should remain usable for a valid catalog edit after malformed source sheetData");
}

void test_replacement_guardrails_and_payload_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-guardrails-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-guardrails-output.xlsx");

    fastxlsx::WorkbookEditorOptions max_cell_options;
    max_cell_options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor max_cell_editor =
        fastxlsx::WorkbookEditor::open(source, max_cell_options);

    check(threw_fastxlsx_error([&] {
        max_cell_editor.replace_sheet_data("Data",
            {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)}});
    }), "replace_sheet_data should enforce max_replacement_cells before commit");
    check_clean_replace_sheet_data_failure_state(
        max_cell_editor, "max_replacement_cells failure");

    max_cell_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(3.0)}});
    check(max_cell_editor.pending_replacement_cell_count() == 1,
        "valid guarded replacement should record one queued cell");
    const std::size_t first_memory =
        max_cell_editor.estimated_pending_replacement_memory_usage();
    check(first_memory > 0,
        "valid guarded replacement should record non-zero estimated memory");

    max_cell_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(4.0)}});
    check(max_cell_editor.pending_change_count() == 2,
        "repeated same-sheet replacement should still count public edit calls");
    check(max_cell_editor.pending_replacement_cell_count() == 1,
        "repeated same-sheet replacement should report only final queued cells");
    check(max_cell_editor.estimated_pending_replacement_memory_usage() > 0,
        "repeated same-sheet replacement should keep final estimated memory");

    max_cell_editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>4</v></c>)",
        "guarded replacement output should use the final queued payload");
    check_not_contains(worksheet_xml, R"(<v>3</v>)",
        "guarded replacement output should drop stale same-sheet payload");

    fastxlsx::WorkbookEditorOptions memory_options;
    memory_options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor memory_editor =
        fastxlsx::WorkbookEditor::open(source, memory_options);
    check(threw_fastxlsx_error([&] {
        memory_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("too large")}});
    }), "replace_sheet_data should enforce replacement_memory_budget_bytes before commit");
    check_clean_replace_sheet_data_failure_state(memory_editor, "memory budget failure");
}

void test_missing_sheet_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-missing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    bool failed = false;
    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_contains(message, "current planned catalog",
            "missing sheet failure should name the planned catalog lookup");
        check_contains(message, "Missing",
            "missing sheet failure should include the requested sheet name");
    }
    check(failed, "replacing a missing sheet should throw FastXlsxError");
    check_clean_replace_sheet_data_failure_state(editor, "missing sheet failure");

    fastxlsx::WorkbookEditorOptions guard_options;
    guard_options.max_replacement_cells = 0;
    fastxlsx::WorkbookEditor guarded_editor =
        fastxlsx::WorkbookEditor::open(source, guard_options);
    failed = false;
    try {
        guarded_editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(2.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "current planned catalog",
            "missing sheet preflight should run before replacement payload guardrails");
    }
    check(failed,
        "guarded missing sheet replacement should throw FastXlsxError");
    check_clean_replace_sheet_data_failure_state(
        guarded_editor, "guarded missing sheet failure");

    // The editor must remain usable after a rejected edit.
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>7</v></c>)",
        "editor should still apply a valid edit after a rejected one");
}

void test_replace_sheet_data_failure_diagnostics_include_context()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-sheet-data-diagnostics-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-sheet-data-diagnostics-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)},
                {fastxlsx::CellValue::text("third")}});
        check(false, "missing-sheet replacement should throw");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "WorkbookEditor::replace_sheet_data() failed",
            "missing-sheet diagnostic should name the public API");
        check_contains(message, "Missing",
            "missing-sheet diagnostic should include the requested sheet");
        check_contains(message, "with 2 rows and 3 cells",
            "missing-sheet diagnostic should include the input shape");
        check_contains(message, "current planned catalog",
            "missing-sheet diagnostic should preserve the root cause");
        check(editor.last_edit_error().has_value() && *editor.last_edit_error() == message,
            "missing-sheet last_edit_error should match the thrown diagnostic");
    }
    check_workbook_editor_public_no_pending_state(
        editor, "missing-sheet diagnostic failure");
    check(editor.pending_replacement_cell_count() == 0,
        "missing-sheet diagnostic failure should not record replacement cells");

    fastxlsx::WorkbookEditorOptions guard_options;
    guard_options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor guarded_editor =
        fastxlsx::WorkbookEditor::open(source, guard_options);
    try {
        guarded_editor.replace_sheet_data("Data",
            {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)}});
        check(false, "guarded replacement should throw");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "WorkbookEditor::replace_sheet_data() failed",
            "guardrail diagnostic should name the public API");
        check_contains(message, "Data",
            "guardrail diagnostic should include the requested sheet");
        check_contains(message, "with 1 rows and 2 cells",
            "guardrail diagnostic should include the input shape");
        check_contains(message, "CellStore max_cells guardrail exceeded",
            "guardrail diagnostic should preserve the root cause");
        check(guarded_editor.last_edit_error().has_value()
                && *guarded_editor.last_edit_error() == message,
            "guardrail last_edit_error should match the thrown diagnostic");
    }
    check_workbook_editor_public_no_pending_state(
        guarded_editor, "guardrail diagnostic failure");
    check(guarded_editor.pending_replacement_cell_count() == 0,
        "guardrail diagnostic failure should not record replacement cells");

    guarded_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
    check(!guarded_editor.last_edit_error().has_value(),
        "successful sheetData replacement should clear prior failure diagnostics");
    guarded_editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<v>9</v>)",
        "editor should remain usable after sheetData replacement diagnostics");
}

void test_save_as_over_source_throws()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-overwrite-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as over the source package should throw FastXlsxError");
}

void test_noop_save_as_preserves_source_package_entries()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_document_properties(
            "fastxlsx-workbook-editor-noop-save-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-noop-save-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check_workbook_editor_public_clean_state(
        editor, "fresh WorkbookEditor before no-op save_as");

    editor.save_as(output);

    check_workbook_editor_public_clean_state(editor, "no-op save_as");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as should preserve decompressed source package entries");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data"),
        "no-op save_as output should keep the Data sheet");
    check(reopened.has_worksheet("Untouched"),
        "no-op save_as output should keep the Untouched sheet");
}

void test_noop_save_as_preserves_failed_edit_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-noop-save-after-error-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-error-copy.xlsx");
    const std::filesystem::path edited_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-error-edited.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(13.0)}});
    }), "missing-sheet edit should fail before no-op save_as");

    const std::optional<std::string> last_error_before_save = editor.last_edit_error();
    check(last_error_before_save.has_value(),
        "failed edit before no-op save_as should record last_edit_error");
    check_workbook_editor_public_no_pending_state(
        editor, "failed edit before no-op save_as");

    editor.save_as(noop_output);

    check(editor.last_edit_error() == last_error_before_save,
        "no-op save_as should preserve a prior failed-edit diagnostic");
    check_workbook_editor_public_no_pending_state(
        editor, "no-op save_as after a failed edit");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after a failed edit should preserve source entries");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after error")}});
    check(!editor.last_edit_error().has_value(),
        "successful edit after no-op save_as should clear the failed-edit diagnostic");
    editor.save_as(edited_output);

    const auto edited_entries = fastxlsx::test::read_zip_entries(edited_output);
    check_contains(edited_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" t="inlineStr"><is><t>after error</t></is></c>)",
        "later edit after no-op save_as should still write replacement data");
}

void test_noop_save_as_preserves_failed_rename_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-noop-save-after-rename-error-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-rename-error-copy.xlsx");
    const std::filesystem::path renamed_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-rename-error-renamed.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "invalid rename should fail before no-op save_as");

    const std::optional<std::string> last_error_before_save = editor.last_edit_error();
    check(last_error_before_save.has_value(),
        "failed rename before no-op save_as should record last_edit_error");
    if (last_error_before_save.has_value()) {
        check_contains(*last_error_before_save, "Bad/Name",
            "failed rename diagnostic should name the rejected sheet");
    }
    check_workbook_editor_public_no_pending_state(
        editor, "failed rename before no-op save_as");

    editor.save_as(noop_output);

    check(editor.last_edit_error() == last_error_before_save,
        "no-op save_as should preserve a prior failed-rename diagnostic");
    check_workbook_editor_public_no_pending_state(
        editor, "no-op save_as after a failed rename");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after a failed rename should preserve source entries");

    editor.rename_sheet("Data", "CleanName");
    check(!editor.last_edit_error().has_value(),
        "successful rename after no-op save_as should clear the failed-rename diagnostic");
    editor.save_as(renamed_output);

    const auto renamed_entries = fastxlsx::test::read_zip_entries(renamed_output);
    check_contains(renamed_entries.at("xl/workbook.xml"), R"(name="CleanName")",
        "later rename after no-op save_as should still update workbook catalog");
}

void test_noop_save_as_keeps_editor_usable_for_later_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-noop-then-edit-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-noop-then-edit-copy.xlsx");
    const std::filesystem::path edited_output =
        artifact("fastxlsx-workbook-editor-noop-then-edit-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.save_as(noop_output);
    check_workbook_editor_public_clean_state(
        editor, "no-op save_as before later edits");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("after noop"),
            fastxlsx::CellValue::number(42.0)}});
    editor.rename_sheet("Data", "AfterNoop");

    check(editor.has_pending_changes(),
        "editor should remain usable for edits after no-op save_as");
    check(editor.pending_change_count() == 2,
        "replacement plus rename after no-op save_as should be queued");
    check(editor.pending_replacement_cell_count() == 2,
        "replacement after no-op save_as should expose pending cells");
    check(editor.has_worksheet("AfterNoop"),
        "planned catalog should expose rename after no-op save_as");
    check(editor.has_source_worksheet("Data"),
        "source catalog should remain available after no-op save_as");

    editor.save_as(edited_output);

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "later edits should not mutate the prior no-op save_as output");

    const auto edited_entries = fastxlsx::test::read_zip_entries(edited_output);
    check_contains(edited_entries.at("xl/workbook.xml"), R"(name="AfterNoop")",
        "save_as after no-op save_as should write the later queued rename");
    check_contains(edited_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" t="inlineStr"><is><t>after noop</t></is></c>)",
        "save_as after no-op save_as should write the later queued text cell");
    check_contains(edited_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1"><v>42</v></c>)",
        "save_as after no-op save_as should write the later queued number cell");
}

void test_failed_save_as_preserves_public_facade_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-save-failure-state-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-save-failure-state-output.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-save-failure-missing-parent") / "output.xlsx";
    const std::filesystem::path file_parent =
        artifact("fastxlsx-workbook-editor-save-failure-file-parent.bin");
    const std::filesystem::path file_parent_output = file_parent / "output.xlsx";
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-save-failure-directory-output");
    write_binary_file(file_parent, "not a directory");
    std::filesystem::create_directories(directory_output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(11.0)}});
    editor.rename_sheet("Data", "RenamedData");

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(99.0)}});
    }), "old source name replacement should fail after queued rename");
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
        workbook_editor_public_save_state_snapshot(editor);
    check(save_state_before_save.last_edit_error.has_value(),
        "pre-save failed edit should leave a public last_edit_error");
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_save =
        editor.worksheet_catalog();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_save =
        editor.pending_worksheet_edits();

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as over the source package should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path{}); }),
        "save_as with an empty output path should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "save_as with a missing parent directory should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(file_parent_output); }),
        "save_as with a non-directory parent should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "save_as to an existing directory should fail without committing output");

    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_save, "failed save_as");

    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_after_save =
        editor.worksheet_catalog();
    check(workbook_editor_catalog_entries_equal(catalog_after_save, catalog_before_save),
        "failed save_as should preserve worksheet catalog");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_after_save =
        editor.pending_worksheet_edits();
    check(workbook_editor_edit_summaries_equal(
              summaries_after_save, summaries_before_save),
        "failed save_as should preserve pending worksheet edit summaries");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "safe save_as after failed save should keep the queued rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>11</v></c>)",
        "safe save_as after failed save should keep the queued sheetData replacement");

    const std::filesystem::path clean_error_output =
        artifact("fastxlsx-workbook-editor-save-failure-clean-error-output.xlsx");
    fastxlsx::WorkbookEditor clean_error_editor = fastxlsx::WorkbookEditor::open(source);
    clean_error_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(12.0)}});
    check(!clean_error_editor.last_edit_error().has_value(),
        "successful edit before save_as failure should leave last_edit_error empty");

    check(threw_fastxlsx_error([&] {
        clean_error_editor.save_as(std::filesystem::path{});
    }), "save_as failure should throw even when no prior edit failure exists");
    check(!clean_error_editor.last_edit_error().has_value(),
        "failed save_as should not create last_edit_error when none existed");

    clean_error_editor.save_as(clean_error_output);
    const auto clean_error_entries = fastxlsx::test::read_zip_entries(clean_error_output);
    check_contains(clean_error_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>12</v></c>)",
        "safe save_as after clean-error failed save should keep the queued replacement");
}

void test_successful_save_as_preserves_public_facade_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-success-save-state-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-success-save-state-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-success-save-state-second.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-success-save-state-third.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(21.0)}});
    editor.rename_sheet("Data", "SavedData");
    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(99.0)}});
    }), "old source name replacement should fail after queued rename before save");

    const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
        workbook_editor_public_save_state_snapshot(editor);
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_save =
        editor.worksheet_catalog();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_save =
        editor.pending_worksheet_edits();
    check(save_state_before_save.last_edit_error.has_value(),
        "pre-save failed edit should leave last_edit_error for successful save_as state test");

    editor.save_as(first_output);

    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_save, "successful save_as");

    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_after_save =
        editor.worksheet_catalog();
    check(workbook_editor_catalog_entries_equal(catalog_after_save, catalog_before_save),
        "successful save_as should preserve worksheet catalog");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_after_save =
        editor.pending_worksheet_edits();
    check(workbook_editor_edit_summaries_equal(
              summaries_after_save, summaries_before_save),
        "successful save_as should preserve pending worksheet edit summaries");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="SavedData")",
        "successful save_as should write the queued rename");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>21</v></c>)",
        "successful save_as should write the queued replacement");

    editor.save_as(second_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(second_entries == first_entries,
        "second save_as without new edits should reuse the preserved pending state");

    editor.replace_sheet_data("SavedData", {{fastxlsx::CellValue::number(31.0)}});
    check(editor.pending_change_count() == save_state_before_save.pending_change_count + 1,
        "follow-up edit after successful save_as should add another pending change");
    check(!editor.last_edit_error().has_value(),
        "follow-up successful edit after save_as should clear the prior edit error");

    editor.save_as(third_output);
    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    check_contains(third_entries.at("xl/workbook.xml"), R"(name="SavedData")",
        "follow-up save_as should keep the queued rename");
    check_contains(third_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>31</v></c>)",
        "follow-up save_as should write the later replacement");
    check_not_contains(third_entries.at("xl/worksheets/sheet1.xml"), R"(<v>21</v>)",
        "follow-up save_as should drop the prior replacement payload");

    const auto first_entries_after_follow_up =
        fastxlsx::test::read_zip_entries(first_output);
    check(first_entries_after_follow_up == first_entries,
        "follow-up edits should not mutate the earlier successful save_as output");
}

void test_empty_rows_emit_empty_sheet_data()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-empty-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-empty-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<sheetData></sheetData>",
        "empty rows should emit an empty sheetData");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "empty replacement should drop old placeholder data");
}

void test_text_uses_inline_strings_and_preserves_shared_strings()
{
    // Build a shared-string source so we can prove the table is preserved, not
    // migrated, when the replacement uses inline strings.
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-shared-source.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("shared-placeholder")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "shared-string source should emit a sharedStrings part");
    const std::string shared_strings_before =
        source_entries.at("xl/sharedStrings.xml");

    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-shared-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("inline-text")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(t="inlineStr")",
        "replacement text should be written as an inline string");
    check_contains(worksheet_xml, "inline-text",
        "replacement text should appear in the worksheet");

    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "existing sharedStrings should be preserved, not migrated or pruned");
}

void test_calc_metadata_requests_recalculation_without_inventing_calcchain()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-calc-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-calc-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/calcChain.xml") == source_entries.end(),
        "streaming writer source should not carry a calcChain");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::formula("SUM(A1:A1)")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "data edit should request workbook recalculation on load");

    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "editor should not invent a calcChain when the source has none");
}

void test_rename_sheet_changes_catalog_name_and_preserves_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string data_sheet_before = source_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "Renamed & Data");

    const std::vector<std::string> names = editor.worksheet_names();
    check(names.size() == 2 && names[0] == "Renamed & Data",
        "planned workbook view should expose the queued rename");
    check(names.size() == 2 && names[1] == "Untouched",
        "planned workbook view should keep the untouched sheet name after rename");
    check(!editor.has_worksheet("Data"),
        "planned workbook view should not expose the original sheet name after rename");
    check(editor.has_worksheet("Renamed & Data"),
        "planned workbook view should expose the queued rename before save");
    const std::vector<std::string> source_names = editor.source_worksheet_names();
    check(source_names.size() == 2 && source_names[0] == "Data",
        "source workbook view should stay on the original catalog after rename");
    check(source_names.size() == 2 && source_names[1] == "Untouched",
        "source workbook view should keep the untouched sheet name after rename");
    check(editor.has_source_worksheet("Data"),
        "source workbook view should still expose the original sheet name after rename");
    check(!editor.has_source_worksheet("Renamed & Data"),
        "source workbook view should not expose the planned rename before save");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "worksheet_catalog should keep both sheets after queued rename");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data",
                "worksheet_catalog should keep the source name for renamed sheet");
            check(catalog[0].planned_name == "Renamed & Data",
                "worksheet_catalog should expose the queued planned name");
            check(catalog[0].renamed,
                "worksheet_catalog should mark the queued rename");
            check(catalog[1].source_name == "Untouched",
                "worksheet_catalog should keep the untouched source name");
            check(catalog[1].planned_name == "Untouched",
                "worksheet_catalog should keep the untouched planned name");
            check(!catalog[1].renamed,
                "worksheet_catalog should not mark untouched sheet as renamed");
        }
    }

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "rename should XML-escape the new sheet name in the output catalog");
    check_not_contains(workbook_xml, R"(name="Data")",
        "rename should drop the old sheet name from the output catalog");

    check(output_entries.at("xl/worksheets/sheet1.xml") == data_sheet_before,
        "rename should preserve the renamed sheet's worksheet bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") == untouched_sheet_before,
        "rename should preserve untouched worksheet bytes");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "rename should preserve content types bytes");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "rename should preserve package relationships bytes");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "rename should preserve workbook relationships bytes");

    // Reopening the output package confirms the new catalog name is the one a
    // reader sees, and the other sheet is unchanged.
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const std::vector<std::string> reopened_names = reopened.worksheet_names();
    check(reopened_names.size() == 2 && reopened_names[0] == "Renamed & Data",
        "reopened output should expose the renamed sheet in catalog order");
    check(reopened_names.size() == 2 && reopened_names[1] == "Untouched",
        "reopened output should keep the untouched sheet name");
    check(!reopened.has_worksheet("Data"),
        "reopened output should no longer expose the old sheet name");
}

void test_replace_sheet_data_uses_planned_catalog_after_rename()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-planned-catalog-source.xlsx");

    {
        const std::filesystem::path rename_only_output =
            artifact("fastxlsx-workbook-editor-planned-catalog-rename-only-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.rename_sheet("Data", "RenamedOnly");

        check(threw_fastxlsx_error([&] {
            editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
        }), "replace_sheet_data should reject the old source name after a planned rename");
        check(editor.pending_change_count() == 1,
            "old-name replace failure should not add a public pending change after rename");
        check_workbook_editor_no_replacement_payload_size_diagnostics(
            editor, "old-name replace failure after rename");
        check(!editor.has_worksheet("Data"),
            "planned workbook inspection should reject the old name after planned rename");
        check(editor.has_worksheet("RenamedOnly"),
            "planned workbook inspection should expose the planned name before save");
        check(editor.has_source_worksheet("Data"),
            "source workbook inspection should still expose the old name after planned rename");
        check(!editor.has_source_worksheet("RenamedOnly"),
            "source workbook inspection should not expose the planned name before save");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2,
                "worksheet_catalog should remain available after old-name replacement failure");
            if (catalog.size() == 2) {
                check(catalog[0].source_name == "Data",
                    "worksheet_catalog should keep source name after old-name failure");
                check(catalog[0].planned_name == "RenamedOnly",
                    "worksheet_catalog should keep planned name after old-name failure");
                check(catalog[0].renamed,
                    "worksheet_catalog should keep rename flag after old-name failure");
            }
        }

        editor.save_as(rename_only_output);
        const auto rename_only_entries = fastxlsx::test::read_zip_entries(rename_only_output);
        check_contains(rename_only_entries.at("xl/workbook.xml"), R"(name="RenamedOnly")",
            "old-name replace failure should preserve the queued catalog rename");
        check_contains(rename_only_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
            "old-name replace failure should preserve the source sheetData");
        check_not_contains(rename_only_entries.at("xl/worksheets/sheet1.xml"), R"(<v>9</v>)",
            "old-name replace failure should not leak rejected replacement cells");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-planned-catalog-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});
        check(editor.pending_replacement_cell_count() == 1,
            "initial replacement should record one pending replacement cell");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2 && catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "Data" && !catalog[0].renamed,
                "replacement-only edit should not change worksheet_catalog");
        }

        editor.rename_sheet("Data", "RenamedData");
        check(editor.pending_change_count() == 2,
            "rename after replacement should add one public pending change");
        check(editor.pending_replacement_cell_count() == 1,
            "rename should migrate the pending replacement diagnostic to the planned name");
        {
            const std::vector<std::string> pending_names =
                editor.pending_replacement_worksheet_names();
            check(pending_names.size() == 1 && pending_names[0] == "RenamedData",
                "rename should migrate pending replacement names to the planned sheet name");
            check(editor.has_pending_replacement("RenamedData"),
                "rename should mark the planned sheet name as pending replacement");
            check(!editor.has_pending_replacement("Data"),
                "rename should stop reporting the old source name as pending replacement");
        }
        check(editor.estimated_pending_replacement_memory_usage() > 0,
            "rename should preserve the pending replacement memory diagnostic");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2 && catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "RenamedData" && catalog[0].renamed,
                "worksheet_catalog should report replace+rename planned catalog state");
        }

        check(threw_fastxlsx_error([&] {
            editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
        }), "replace_sheet_data should keep resolving through the planned catalog");
        check(editor.pending_change_count() == 2,
            "old-name replace failure should preserve prior replace+rename count");
        check(editor.pending_replacement_cell_count() == 1,
            "old-name replace failure should preserve the prior replacement diagnostic");

        editor.replace_sheet_data("RenamedData",
            {{fastxlsx::CellValue::number(2.0), fastxlsx::CellValue::number(3.0)}});
        check(editor.pending_change_count() == 3,
            "new planned-name replacement should add one public pending change");
        check(editor.pending_replacement_cell_count() == 2,
            "new planned-name replacement should overwrite the stale pre-rename diagnostic");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2 && catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "RenamedData" && catalog[0].renamed,
                "planned-name replacement should not change worksheet_catalog rename mapping");
        }
        {
            const std::vector<std::string> pending_names =
                editor.pending_replacement_worksheet_names();
            check(pending_names.size() == 1 && pending_names[0] == "RenamedData",
                "planned-name replacement should keep one pending replacement name");
        }

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
            "planned-name replacement should preserve the queued rename in output");
        check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
            "planned-name replacement should drop the old catalog name in output");

        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<c r="A1"><v>2</v></c>)",
            "planned-name replacement should write the final A1 cell");
        check_contains(worksheet_xml, R"(<c r="B1"><v>3</v></c>)",
            "planned-name replacement should write the final B1 cell");
        check_not_contains(worksheet_xml, R"(<v>1</v>)",
            "planned-name replacement should drop the stale pre-rename payload");
        check_not_contains(worksheet_xml, R"(<v>9</v>)",
            "planned-name replacement should drop the rejected old-name payload");

        fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
        check(reopened.has_worksheet("RenamedData"),
            "reopened output should expose the planned catalog name");
        check(!reopened.has_worksheet("Data"),
            "reopened output should not expose the old source name");
    }
}

void test_rename_back_to_source_name_restores_public_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-back-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-back-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(11.0)}});
    const std::size_t replacement_memory =
        editor.estimated_pending_replacement_memory_usage();
    check(replacement_memory > 0,
        "initial replacement before rename-back should record memory");

    editor.rename_sheet("Data", "Temporary");
    check(editor.has_worksheet("Temporary"),
        "first rename should expose the temporary planned name");
    check(!editor.has_worksheet("Data"),
        "first rename should hide the source name from the planned catalog");
    check(editor.has_pending_replacement("Temporary"),
        "first rename should migrate replacement diagnostics to the planned name");

    editor.rename_sheet("Temporary", "Data");
    check(editor.pending_change_count() == 3,
        "rename-back should count as a public edit without committing");
    check(editor.has_worksheet("Data"),
        "rename-back should restore the source name in the planned catalog");
    check(!editor.has_worksheet("Temporary"),
        "rename-back should remove the temporary planned name");
    check(editor.has_source_worksheet("Data"),
        "rename-back should not change the source catalog view");
    check(editor.pending_replacement_cell_count() == 1,
        "rename-back should preserve the queued replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage() == replacement_memory,
        "rename-back should preserve the replacement memory diagnostic");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "Data",
            "rename-back should migrate replacement diagnostics back to the source name");
        check(editor.has_pending_replacement("Data"),
            "rename-back should report the restored source name as data-replaced");
        check(!editor.has_pending_replacement("Temporary"),
            "rename-back should stop reporting the temporary planned name as data-replaced");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "rename-back should preserve the catalog entry count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" && catalog[0].planned_name == "Data",
                "rename-back should restore the source-to-planned mapping");
            check(!catalog[0].renamed,
                "rename-back should clear the public renamed flag");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "rename-back with replacement should keep one edit summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" && summaries[0].planned_name == "Data",
                "rename-back summary should restore source and planned names");
            check(!summaries[0].renamed,
                "rename-back summary should not remain marked as renamed");
            check(summaries[0].sheet_data_replaced,
                "rename-back summary should preserve sheetData replacement");
            check(summaries[0].replacement_cell_count == 1,
                "rename-back summary should preserve replacement cell count");
            check(summaries[0].estimated_replacement_memory_usage == replacement_memory,
                "rename-back summary should preserve replacement memory");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "rename-back output should use the restored source name");
    check_not_contains(output_entries.at("xl/workbook.xml"), "Temporary",
        "rename-back output should not leak the transient planned name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<c r="A1"><v>11</v></c>)",
        "rename-back output should preserve the queued replacement payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "rename-back output should drop the old source sheetData");
}

void test_rename_chain_back_to_source_name_clears_rename_only_summary()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-chain-back-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-chain-back-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TemporaryA");
    editor.rename_sheet("TemporaryA", "TemporaryB");
    editor.rename_sheet("TemporaryB", "Data");

    check(editor.has_pending_changes(),
        "rename-only chain should still report queued public edit calls");
    check(editor.pending_change_count() == 3,
        "rename-only chain back to source should count each successful public edit");
    check(editor.has_worksheet("Data"),
        "rename-only chain back should restore the source name in planned inspection");
    check(!editor.has_worksheet("TemporaryA"),
        "rename-only chain back should remove the first transient planned name");
    check(!editor.has_worksheet("TemporaryB"),
        "rename-only chain back should remove the second transient planned name");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "rename-only chain back");
    check(editor.pending_worksheet_edits().empty(),
        "rename-only chain back to the source name should clear rename-only summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "rename-only chain back should preserve catalog entry count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" && catalog[0].planned_name == "Data",
                "rename-only chain back should restore source-to-planned mapping");
            check(!catalog[0].renamed,
                "rename-only chain back should clear the public renamed flag");
        }
    }

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "failed rename after chain-back should throw FastXlsxError");
    {
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "failed rename after chain-back should record last_edit_error");
        if (last_error.has_value()) {
            check_contains(*last_error, "Data",
                "failed rename after chain-back diagnostic should include restored source name");
            check_contains(*last_error, "Untouched",
                "failed rename after chain-back diagnostic should include rejected target name");
        }
    }
    check(editor.pending_change_count() == 3,
        "failed rename after chain-back should not add a public pending change");
    check(editor.pending_worksheet_edits().empty(),
        "failed rename after chain-back should preserve empty rename-only summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "failed rename after chain-back should preserve catalog entry count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" && catalog[0].planned_name == "Data",
                "failed rename after chain-back should preserve restored catalog mapping");
            check(!catalog[0].renamed,
                "failed rename after chain-back should preserve cleared renamed flag");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "rename-only chain back output should use the restored source name");
    check_not_contains(workbook_xml, "TemporaryA",
        "rename-only chain back output should not leak the first transient name");
    check_not_contains(workbook_xml, "TemporaryB",
        "rename-only chain back output should not leak the second transient name");
}

void test_replacement_after_rename_chain_back_failure_uses_restored_name()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-chain-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-chain-replace-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TemporaryA");
    editor.rename_sheet("TemporaryA", "TemporaryB");
    editor.rename_sheet("TemporaryB", "Data");

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "duplicate rename after chain-back should throw FastXlsxError");
    check(editor.last_edit_error().has_value(),
        "duplicate rename after chain-back should leave last_edit_error");
    check(editor.pending_worksheet_edits().empty(),
        "duplicate rename after chain-back should preserve empty summaries");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("after chain-back failure"),
            fastxlsx::CellValue::number(31.0)}});

    check(!editor.last_edit_error().has_value(),
        "successful replacement after chain-back failure should clear last_edit_error");
    check(editor.pending_change_count() == 4,
        "replacement after three successful renames should add one public edit call");
    check(editor.pending_replacement_cell_count() == 2,
        "replacement after chain-back failure should record final replacement cells");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "Data",
            "replacement after chain-back failure should use the restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "replacement after chain-back failure should create one summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" && summaries[0].planned_name == "Data",
                "replacement after chain-back failure should keep restored catalog names");
            check(!summaries[0].renamed,
                "replacement after chain-back failure should not reintroduce rename flag");
            check(summaries[0].sheet_data_replaced,
                "replacement after chain-back failure should report sheetData replacement");
            check(summaries[0].replacement_cell_count == 2,
                "replacement after chain-back failure should report replacement cell count");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "replacement after chain-back failure output should keep restored source name");
    check_not_contains(workbook_xml, "TemporaryA",
        "replacement after chain-back failure output should not leak first transient name");
    check_not_contains(workbook_xml, "TemporaryB",
        "replacement after chain-back failure output should not leak second transient name");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>after chain-back failure</t></is></c>)",
        "replacement after chain-back failure should write final text cell");
    check_contains(worksheet_xml, R"(<c r="B1"><v>31</v></c>)",
        "replacement after chain-back failure should write final number cell");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "replacement after chain-back failure should remove old sheetData");
}

void test_failed_rename_preserves_pending_replacement_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-failure-diagnostics-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-failure-diagnostics-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});

    const std::size_t memory_after_initial_replacement =
        editor.estimated_pending_replacement_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summary_after_initial_replacement =
        editor.pending_worksheet_edits();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_after_initial_replacement =
        editor.worksheet_catalog();
    check(editor.pending_change_count() == 1,
        "initial replacement before failed rename should add one public pending change");
    check(editor.pending_replacement_cell_count() == 1,
        "initial replacement before failed rename should record one cell");
    check(memory_after_initial_replacement > 0,
        "initial replacement before failed rename should record memory");

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "duplicate rename after a replacement should throw FastXlsxError");
    {
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "duplicate rename failure should record a public last_edit_error");
        if (last_error.has_value()) {
            check_contains(*last_error, "Data",
                "duplicate rename last_edit_error should include the source sheet");
            check_contains(*last_error, "Untouched",
                "duplicate rename last_edit_error should include the rejected target sheet");
        }
    }
    check(editor.pending_change_count() == 1,
        "duplicate rename failure should not add a public pending change");
    check(editor.pending_replacement_cell_count() == 1,
        "duplicate rename failure should preserve the old-name replacement diagnostic");
    check(editor.estimated_pending_replacement_memory_usage() == memory_after_initial_replacement,
        "duplicate rename failure should preserve the replacement memory diagnostic");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == catalog_after_initial_replacement.size(),
            "duplicate rename failure should preserve catalog entry count");
        if (catalog.size() == 2 && catalog_after_initial_replacement.size() == 2) {
            check(catalog[0].source_name == catalog_after_initial_replacement[0].source_name,
                "duplicate rename failure should preserve catalog source name");
            check(catalog[0].planned_name == catalog_after_initial_replacement[0].planned_name,
                "duplicate rename failure should preserve catalog planned name");
            check(catalog[0].renamed == catalog_after_initial_replacement[0].renamed,
                "duplicate rename failure should preserve catalog rename flag");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == summary_after_initial_replacement.size(),
            "duplicate rename failure should preserve summary count");
        if (summaries.size() == 1 && summary_after_initial_replacement.size() == 1) {
            check(summaries[0].source_name == summary_after_initial_replacement[0].source_name,
                "duplicate rename failure should preserve summary source name");
            check(summaries[0].planned_name == summary_after_initial_replacement[0].planned_name,
                "duplicate rename failure should preserve summary planned name");
            check(summaries[0].renamed == summary_after_initial_replacement[0].renamed,
                "duplicate rename failure should preserve summary rename flag");
            check(summaries[0].sheet_data_replaced ==
                    summary_after_initial_replacement[0].sheet_data_replaced,
                "duplicate rename failure should preserve summary replacement flag");
            check(summaries[0].replacement_cell_count ==
                    summary_after_initial_replacement[0].replacement_cell_count,
                "duplicate rename failure should preserve summary cell count");
            check(summaries[0].estimated_replacement_memory_usage ==
                    summary_after_initial_replacement[0].estimated_replacement_memory_usage,
                "duplicate rename failure should preserve summary memory estimate");
        }
    }

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "invalid rename after a replacement should throw FastXlsxError");
    {
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "invalid rename failure should record a public last_edit_error");
        if (last_error.has_value()) {
            check_contains(*last_error, "Data",
                "invalid rename last_edit_error should include the source sheet");
            check_contains(*last_error, "Bad/Name",
                "invalid rename last_edit_error should include the rejected target sheet");
            check_not_contains(*last_error, "Untouched",
                "invalid rename last_edit_error should replace the previous duplicate diagnostic");
        }
    }
    check(editor.pending_change_count() == 1,
        "invalid rename failure should not add a public pending change");
    check(editor.pending_replacement_cell_count() == 1,
        "invalid rename failure should preserve the pending replacement diagnostic");
    check(editor.estimated_pending_replacement_memory_usage() == memory_after_initial_replacement,
        "invalid rename failure should preserve replacement memory");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == catalog_after_initial_replacement.size(),
            "invalid rename failure should preserve catalog entry count");
        if (catalog.size() == 2 && catalog_after_initial_replacement.size() == 2) {
            check(catalog[0].source_name == catalog_after_initial_replacement[0].source_name,
                "invalid rename failure should preserve catalog source name");
            check(catalog[0].planned_name == catalog_after_initial_replacement[0].planned_name,
                "invalid rename failure should preserve catalog planned name");
            check(catalog[0].renamed == catalog_after_initial_replacement[0].renamed,
                "invalid rename failure should preserve catalog rename flag");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == summary_after_initial_replacement.size(),
            "invalid rename failure should preserve summary count");
        if (summaries.size() == 1 && summary_after_initial_replacement.size() == 1) {
            check(summaries[0].source_name == summary_after_initial_replacement[0].source_name,
                "invalid rename failure should preserve summary source name");
            check(summaries[0].planned_name == summary_after_initial_replacement[0].planned_name,
                "invalid rename failure should preserve summary planned name");
            check(summaries[0].renamed == summary_after_initial_replacement[0].renamed,
                "invalid rename failure should preserve summary rename flag");
            check(summaries[0].sheet_data_replaced ==
                    summary_after_initial_replacement[0].sheet_data_replaced,
                "invalid rename failure should preserve summary replacement flag");
            check(summaries[0].replacement_cell_count ==
                    summary_after_initial_replacement[0].replacement_cell_count,
                "invalid rename failure should preserve summary cell count");
            check(summaries[0].estimated_replacement_memory_usage ==
                    summary_after_initial_replacement[0].estimated_replacement_memory_usage,
                "invalid rename failure should preserve summary memory estimate");
        }
    }

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(4.0), fastxlsx::CellValue::number(5.0)}});
    check(editor.pending_change_count() == 2,
        "valid replacement after failed renames should add one pending change");
    check(editor.pending_replacement_cell_count() == 2,
        "valid replacement after failed renames should overwrite the stale payload");
    check(editor.estimated_pending_replacement_memory_usage() > 0,
        "valid replacement after failed renames should keep replacement memory visible");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "failed renames should leave the original Data catalog name in output");
    check_not_contains(output_entries.at("xl/workbook.xml"), "Bad/Name",
        "failed invalid rename should not leak the rejected name into the output catalog");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>4</v></c>)",
        "replacement after failed renames should write the final A1 cell");
    check_contains(worksheet_xml, R"(<c r="B1"><v>5</v></c>)",
        "replacement after failed renames should write the final B1 cell");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "replacement after failed renames should drop the stale pre-failure payload");
}

void test_replace_image_updates_target_media_bytes_and_preserves_other_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images("fastxlsx-workbook-editor-image-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-image-replace-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::filesystem::path replacement_jpeg_path =
        repository_asset("docs/assets/donation/zhifubao.jpg");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    const std::string replacement_jpeg_bytes = fastxlsx::test::read_file(replacement_jpeg_path);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string data_sheet_before = source_entries.at("xl/worksheets/sheet1.xml");
    const std::string picture_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string picture_sheet_rels_before =
        source_entries.at("xl/worksheets/_rels/sheet2.xml.rels");
    const std::string drawing_before = source_entries.at("xl/drawings/drawing1.xml");
    const std::string drawing_rels_before =
        source_entries.at("xl/drawings/_rels/drawing1.xml.rels");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
              editor.replace_image("xl/worksheets/sheet1.xml", replacement_png_path);
          }),
        "replacing a non-media target should throw");
    check(editor.pending_change_count() == 0,
        "failed image replacement should not increment pending change count");
    check(!editor.has_pending_changes(),
        "failed image replacement should not queue pending changes");

    check(threw_fastxlsx_error([&] {
              editor.replace_image("xl/media/image1.png", replacement_jpeg_path);
          }),
        "replacing a PNG target with JPEG bytes should throw");
    check(editor.pending_change_count() == 0,
        "failed format-mismatch image replacement should not increment pending change count");
    check(!editor.has_pending_changes(),
        "failed format-mismatch image replacement should not queue pending changes");
    check(editor.last_edit_error().has_value(),
        "failed image replacement should record a public edit diagnostic");

    editor.replace_image("xl/media/image1.png", replacement_png_path);
    editor.replace_image("xl/media/image2.jpg", as_bytes(replacement_jpeg_bytes));
    check(!editor.last_edit_error().has_value(),
        "successful image replacement should clear the public edit diagnostic");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "PNG media bytes should be replaced from file");
    check(output_entries.at("xl/media/image2.jpg") == replacement_jpeg_bytes,
        "JPEG media bytes should be replaced from memory");
    check(output_entries.at("xl/worksheets/sheet1.xml") == data_sheet_before,
        "data worksheet bytes should be preserved");
    check(output_entries.at("xl/worksheets/sheet2.xml") == picture_sheet_before,
        "picture worksheet bytes should be preserved");
    check(output_entries.at("xl/worksheets/_rels/sheet2.xml.rels") == picture_sheet_rels_before,
        "picture worksheet relationships should be preserved");
    check(output_entries.at("xl/drawings/drawing1.xml") == drawing_before,
        "drawing XML should be preserved");
    check(output_entries.at("xl/drawings/_rels/drawing1.xml.rels") == drawing_rels_before,
        "drawing relationships should be preserved");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "content types should be preserved");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "package relationships should be preserved");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "workbook relationships should be preserved");
}

void test_replace_image_rejects_missing_or_mismatched_targets()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images("fastxlsx-workbook-editor-image-reject-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-image-reject-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::filesystem::path replacement_jpeg_path =
        repository_asset("docs/assets/donation/zhifubao.jpg");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
              editor.replace_image("xl/media/missing.png", replacement_png_path);
          }),
        "replacing a missing media target should throw");
    check(editor.pending_change_count() == 0,
        "missing-target image replacement should not increment pending change count");
    check(!editor.has_pending_changes(),
        "missing-target image replacement should not queue pending changes");

    check(threw_fastxlsx_error([&] {
              editor.replace_image("xl/media/image1.png", replacement_jpeg_path);
          }),
        "replacing a PNG target with JPEG bytes should throw");
    check(editor.pending_change_count() == 0,
        "format-mismatch image replacement should not increment pending change count");
    check(!editor.has_pending_changes(),
        "format-mismatch image replacement should not queue pending changes");

    editor.replace_image("xl/media/image1.png", replacement_png_path);
    editor.replace_image("xl/media/image2.jpg", replacement_jpeg_path);
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/media/image1.png") == fastxlsx::test::read_file(replacement_png_path),
        "PNG replacement should remain usable after rejected attempts");
    check(output_entries.at("xl/media/image2.jpg") == fastxlsx::test::read_file(replacement_jpeg_path),
        "JPEG replacement should remain usable after rejected attempts");
}

void test_replace_image_failure_diagnostics_include_context()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-diagnostics-source.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::filesystem::path replacement_jpeg_path =
        repository_asset("docs/assets/donation/zhifubao.jpg");
    const std::string replacement_jpeg_bytes = fastxlsx::test::read_file(replacement_jpeg_path);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    try {
        editor.replace_image("xl/media/missing.png", replacement_png_path);
        check(false, "missing-target image replacement should throw");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "WorkbookEditor::replace_image() failed",
            "missing-target diagnostic should name the public API");
        check_contains(message, "xl/media/missing.png",
            "missing-target diagnostic should include the requested media part");
        check_contains(message, replacement_png_path.generic_string(),
            "missing-target diagnostic should include the replacement file path");
        check_contains(message, "image target is not present in current package",
            "missing-target diagnostic should preserve the root cause");
        check(editor.last_edit_error().has_value() && *editor.last_edit_error() == message,
            "missing-target last_edit_error should match the thrown diagnostic");
    }
    check(editor.pending_change_count() == 0,
        "missing-target diagnostic failure should not increment pending changes");
    check(!editor.has_pending_changes(),
        "missing-target diagnostic failure should not queue pending changes");

    try {
        editor.replace_image("xl/media/image1.png", as_bytes(replacement_jpeg_bytes));
        check(false, "memory format-mismatch image replacement should throw");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "WorkbookEditor::replace_image() failed",
            "memory diagnostic should name the public API");
        check_contains(message, "xl/media/image1.png",
            "memory diagnostic should include the requested media part");
        check_contains(message,
            std::string("from memory bytes (") + std::to_string(replacement_jpeg_bytes.size())
                + " bytes)",
            "memory diagnostic should include the staged byte count");
        check_contains(message, "image replacement format does not match target media part",
            "memory diagnostic should preserve the root cause");
        check(editor.last_edit_error().has_value() && *editor.last_edit_error() == message,
            "memory last_edit_error should match the thrown diagnostic");
    }
    check(editor.pending_change_count() == 0,
        "memory diagnostic failure should not increment pending changes");
    check(!editor.has_pending_changes(),
        "memory diagnostic failure should not queue pending changes");

    editor.replace_image("xl/media/image1.png", replacement_png_path);
    check(!editor.last_edit_error().has_value(),
        "successful image replacement should clear prior failure diagnostics");
}

void test_replace_image_file_save_failure_preserves_pending_state()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-file-save-failure-source.xlsx");
    const std::filesystem::path staged_png_path =
        artifact("fastxlsx-workbook-editor-image-file-save-failure-staged.png");
    const std::filesystem::path failed_output =
        artifact("fastxlsx-workbook-editor-image-file-save-failure-output.xlsx");
    const std::filesystem::path recovered_output =
        artifact("fastxlsx-workbook-editor-image-file-save-recovered-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    write_binary_file(staged_png_path, replacement_png_bytes);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_image("xl/media/image1.png", staged_png_path);
    check(editor.has_pending_changes(),
        "file-backed image replacement should queue pending work before save_as");
    check(editor.pending_change_count() == 1,
        "file-backed image replacement should increment public pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful file-backed image replacement should leave no diagnostic");

    std::filesystem::remove(staged_png_path);
    check(threw_fastxlsx_error([&] { editor.save_as(failed_output); }),
        "save_as should fail if the staged replacement image file disappears");
    check(editor.has_pending_changes(),
        "failed file-backed image save_as should preserve pending work");
    check(editor.pending_change_count() == 1,
        "failed file-backed image save_as should preserve pending change count");
    check(!editor.last_edit_error().has_value(),
        "failed save_as should not create last_edit_error for missing staged image file");

    write_binary_file(staged_png_path, replacement_png_bytes);
    editor.save_as(recovered_output);
    check(editor.has_pending_changes(),
        "successful file-backed image save_as should preserve pending work for another save_as");
    check(editor.pending_change_count() == 1,
        "successful file-backed image save_as should preserve pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful file-backed image save_as should not create last_edit_error");

    const auto output_entries = fastxlsx::test::read_zip_entries(recovered_output);
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "restored staged image file should let save_as write the queued replacement");

    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-image-file-save-second-output.xlsx");
    editor.save_as(second_output);

    const auto second_output_entries = fastxlsx::test::read_zip_entries(second_output);
    check(second_output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "file-backed image replacement should remain reusable for a second save_as");
}

void test_replace_image_file_crc_failure_preserves_pending_state()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-file-crc-failure-source.xlsx");
    const std::filesystem::path staged_png_path =
        artifact("fastxlsx-workbook-editor-image-file-crc-failure-staged.png");
    const std::filesystem::path failed_output =
        artifact("fastxlsx-workbook-editor-image-file-crc-failure-output.xlsx");
    const std::filesystem::path recovered_output =
        artifact("fastxlsx-workbook-editor-image-file-crc-recovered-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    std::string corrupted_png_bytes = replacement_png_bytes;
    check(!corrupted_png_bytes.empty(),
        "replacement fixture should contain bytes for CRC mutation");
    const std::size_t mutation_index = corrupted_png_bytes.size() / 2U;
    corrupted_png_bytes[mutation_index] = static_cast<char>(
        static_cast<unsigned char>(corrupted_png_bytes[mutation_index]) ^ 0x01U);
    write_binary_file(staged_png_path, replacement_png_bytes);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_image("xl/media/image1.png", staged_png_path);
    check(editor.has_pending_changes(),
        "file-backed image replacement should queue pending work before CRC failure");
    check(editor.pending_change_count() == 1,
        "file-backed image replacement should increment pending count before CRC failure");
    check(!editor.last_edit_error().has_value(),
        "successful file-backed image replacement should leave no diagnostic before CRC failure");

    write_binary_file(staged_png_path, corrupted_png_bytes);
    try {
        editor.save_as(failed_output);
        check(false, "save_as should fail if the staged replacement image file changes");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "CRC32 changed after staging",
            "changed staged image failure should report the CRC contract");
    }
    check(editor.has_pending_changes(),
        "CRC-failed file-backed image save_as should preserve pending work");
    check(editor.pending_change_count() == 1,
        "CRC-failed file-backed image save_as should preserve pending change count");
    check(!editor.last_edit_error().has_value(),
        "CRC-failed save_as should not create last_edit_error");

    write_binary_file(staged_png_path, replacement_png_bytes);
    editor.save_as(recovered_output);
    check(editor.has_pending_changes(),
        "successful save_as after CRC recovery should preserve pending work");
    check(editor.pending_change_count() == 1,
        "successful save_as after CRC recovery should preserve pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful save_as after CRC recovery should not create last_edit_error");

    const auto output_entries = fastxlsx::test::read_zip_entries(recovered_output);
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "restored staged image file should write the original queued replacement");
}

void test_replace_image_same_part_later_replacement_wins()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-replace-latest-source.xlsx");
    const std::filesystem::path staged_png_path =
        artifact("fastxlsx-workbook-editor-image-replace-latest-staged.png");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-image-replace-latest-output.xlsx");

    const std::filesystem::path first_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string first_png_bytes = fastxlsx::test::read_file(first_png_path);
    const std::span<const std::byte> second_png_span = fastxlsx::test::tiny_png_bytes();
    std::string second_png_bytes;
    second_png_bytes.assign(reinterpret_cast<const char*>(second_png_span.data()),
        reinterpret_cast<const char*>(second_png_span.data()) + second_png_span.size());
    check(first_png_bytes != second_png_bytes,
        "same-part image replacement fixtures should have distinct bytes");

    write_binary_file(staged_png_path, first_png_bytes);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_image("xl/media/image1.png", staged_png_path);
    editor.replace_image("xl/media/image1.png", second_png_span);
    check(editor.has_pending_changes(),
        "same-part image replacement override should leave pending work");
    check(editor.pending_change_count() == 2,
        "same-part image replacement override should still count both public edit calls");
    check(!editor.last_edit_error().has_value(),
        "successful same-part image replacement override should leave no diagnostic");

    std::filesystem::remove(staged_png_path);
    editor.save_as(output);
    check(!editor.last_edit_error().has_value(),
        "successful save_as after same-part override should not create last_edit_error");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/media/image1.png") == second_png_bytes,
        "later memory-backed image replacement should override earlier file-backed replacement");
    check(output_entries.at("xl/media/image1.png") != first_png_bytes,
        "superseded file-backed image replacement should not leak into output");
}

void test_replace_image_memory_source_copies_bytes_before_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_two_images(
            "fastxlsx-workbook-editor-image-memory-lifetime-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-image-memory-lifetime-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    std::string caller_buffer = replacement_png_bytes;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_image("xl/media/image1.png", as_bytes(caller_buffer));
    check(editor.has_pending_changes(),
        "memory-backed image replacement should queue pending work before save_as");
    check(editor.pending_change_count() == 1,
        "memory-backed image replacement should increment public pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful memory-backed image replacement should leave no diagnostic");

    caller_buffer.assign(caller_buffer.size(), '\0');
    editor.save_as(output);

    check(editor.has_pending_changes(),
        "successful save_as should preserve memory-backed pending public edit state");
    check(editor.pending_change_count() == 1,
        "successful save_as should preserve memory-backed pending change count");
    check(!editor.last_edit_error().has_value(),
        "successful memory-backed save_as should not create last_edit_error");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "memory-backed image replacement should copy caller bytes before save_as");
    check(output_entries.at("xl/media/image1.png") != caller_buffer,
        "memory-backed image replacement should not observe later caller buffer mutation");

    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-image-memory-lifetime-second-output.xlsx");
    editor.save_as(second_output);

    const auto second_output_entries = fastxlsx::test::read_zip_entries(second_output);
    check(second_output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "memory-backed image replacement should remain reusable for a second save_as");
}

void test_public_workbook_editor_editing_end_to_end_smoke()
{
    const std::filesystem::path source =
        write_public_editing_e2e_source("fastxlsx-workbook-editor-editing-e2e-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-editing-e2e-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string picture_sheet_before = source_entries.at("xl/worksheets/sheet3.xml");
    const std::string picture_sheet_rels_before =
        source_entries.at("xl/worksheets/_rels/sheet3.xml.rels");
    const std::string drawing_before = source_entries.at("xl/drawings/drawing1.xml");
    const std::string drawing_rels_before =
        source_entries.at("xl/drawings/_rels/drawing1.xml.rels");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");
    const std::string core_props_before = source_entries.at("docProps/core.xml");
    const std::string app_props_before = source_entries.at("docProps/app.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "EditedData");

    fastxlsx::WorksheetEditor edited_data = editor.worksheet("EditedData");
    check(edited_data.name() == "EditedData",
        "renamed materialized WorksheetEditor should expose the planned sheet name");
    edited_data.set_cell(1, 1, fastxlsx::CellValue::text("materialized-edit"));
    edited_data.set_cell(2, 2, fastxlsx::CellValue::number(42.0));

    editor.replace_sheet_data("ReplaceMe",
        {{fastxlsx::CellValue::text("sheetdata-final"),
            fastxlsx::CellValue::number(7.0)}});
    editor.replace_image("xl/media/image1.png", replacement_png_path);

    check(editor.has_pending_changes(),
        "combined public editing smoke should expose pending work before save_as");
    check(editor.pending_change_count() == 3,
        "combined public editing smoke should count rename, sheetData, and image edits");
    check(editor.pending_materialized_cell_count() == edited_data.cell_count(),
        "combined public editing smoke should report dirty materialized cell count");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "ReplaceMe",
            "combined public editing smoke should track only the replaced sheetData sheet");
    }

    editor.save_as(output);
    check(editor.pending_change_count() == 4,
        "save_as should add the materialized worksheet flush to the public edit count");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string data_sheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string replaced_sheet_xml = output_entries.at("xl/worksheets/sheet2.xml");

    check_contains(workbook_xml, R"(name="EditedData")",
        "combined public editing smoke should save the renamed sheet catalog entry");
    check_contains(workbook_xml, R"(name="ReplaceMe")",
        "combined public editing smoke should preserve the replacement sheet catalog entry");
    check_contains(workbook_xml, R"(name="Pictures")",
        "combined public editing smoke should preserve the image sheet catalog entry");
    check_not_contains(workbook_xml, R"(name="Data")",
        "combined public editing smoke should remove the old sheet name");

    check_contains(data_sheet_xml, "materialized-edit",
        "combined public editing smoke should persist materialized text edits");
    check_contains(data_sheet_xml, R"(<c r="B2"><v>42</v></c>)",
        "combined public editing smoke should persist materialized numeric edits");
    check_not_contains(data_sheet_xml, "placeholder-a1",
        "combined public editing smoke should replace the edited source cell");
    check_contains(data_sheet_xml, R"(<dimension ref="A1:B2"/>)",
        "combined public editing smoke should refresh materialized worksheet dimension");

    check_contains(replaced_sheet_xml, "sheetdata-final",
        "combined public editing smoke should persist sheetData replacement text");
    check_contains(replaced_sheet_xml, R"(<c r="B1"><v>7</v></c>)",
        "combined public editing smoke should persist sheetData replacement number");
    check_not_contains(replaced_sheet_xml, "replace-old",
        "combined public editing smoke should drop old sheetData rows");

    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "combined public editing smoke should replace the target media bytes");
    check(output_entries.at("xl/worksheets/sheet3.xml") == picture_sheet_before,
        "combined public editing smoke should preserve the picture worksheet XML");
    check(output_entries.at("xl/worksheets/_rels/sheet3.xml.rels") == picture_sheet_rels_before,
        "combined public editing smoke should preserve picture worksheet relationships");
    check(output_entries.at("xl/drawings/drawing1.xml") == drawing_before,
        "combined public editing smoke should preserve drawing XML");
    check(output_entries.at("xl/drawings/_rels/drawing1.xml.rels") == drawing_rels_before,
        "combined public editing smoke should preserve drawing relationships");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "combined public editing smoke should preserve content types for same-format edits");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "combined public editing smoke should preserve package relationships");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "combined public editing smoke should preserve workbook relationships");
    check(output_entries.at("docProps/core.xml") == core_props_before,
        "combined public editing smoke should preserve core document properties");
    check(output_entries.at("docProps/app.xml") == app_props_before,
        "combined public editing smoke should preserve app document properties");
}

void test_public_workbook_editor_combined_failed_save_as_preserves_state()
{
    const std::filesystem::path source =
        write_public_editing_e2e_source("fastxlsx-workbook-editor-combined-failed-save-source.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-combined-failed-save-missing-parent") / "out.xlsx";
    const std::filesystem::path safe_output =
        artifact("fastxlsx-workbook-editor-combined-failed-save-recovered-output.xlsx");
    std::filesystem::remove_all(missing_parent_output.parent_path());

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    std::string staged_image_bytes = replacement_png_bytes;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "RecoveredData");
    fastxlsx::WorksheetEditor recovered_data = editor.worksheet("RecoveredData");
    recovered_data.set_cell(1, 1, fastxlsx::CellValue::text("dirty-before-failed-save"));
    recovered_data.set_cell(3, 3, fastxlsx::CellValue::number(123.0));
    editor.replace_sheet_data("ReplaceMe",
        {{fastxlsx::CellValue::text("replacement-before-failed-save"),
            fastxlsx::CellValue::number(88.0)}});
    editor.replace_image("xl/media/image1.png", as_bytes(staged_image_bytes));
    staged_image_bytes.assign(staged_image_bytes.size(), '\0');

    check(editor.has_pending_changes(),
        "combined failed save_as recovery should queue mixed public edits first");
    check(editor.pending_change_count() == 3,
        "combined failed save_as recovery should count rename, sheetData, and image before flush");
    check(editor.pending_materialized_cell_count() == recovered_data.cell_count(),
        "combined failed save_as recovery should expose dirty materialized cells before failure");
    check(!editor.last_edit_error().has_value(),
        "combined failed save_as recovery should start with no public edit diagnostic");

    const std::size_t pending_count_before_failure = editor.pending_change_count();
    const std::size_t replacement_cells_before_failure =
        editor.pending_replacement_cell_count();
    const std::size_t replacement_memory_before_failure =
        editor.estimated_pending_replacement_memory_usage();
    const std::size_t materialized_cells_before_failure =
        editor.pending_materialized_cell_count();
    const std::size_t materialized_memory_before_failure =
        editor.estimated_pending_materialized_memory_usage();
    const std::vector<std::string> replacement_names_before_failure =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> materialized_names_before_failure =
        editor.pending_materialized_worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_failure =
        editor.worksheet_catalog();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_failure =
        editor.pending_worksheet_edits();

    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "combined save_as should fail before dirty materialized flush when parent is missing");

    check(editor.pending_change_count() == pending_count_before_failure,
        "failed combined save_as should preserve pending public edit count");
    check(editor.pending_replacement_cell_count() == replacement_cells_before_failure,
        "failed combined save_as should preserve replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage() ==
            replacement_memory_before_failure,
        "failed combined save_as should preserve replacement memory estimate");
    check(editor.pending_materialized_cell_count() == materialized_cells_before_failure,
        "failed combined save_as should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() ==
            materialized_memory_before_failure,
        "failed combined save_as should preserve dirty materialized memory estimate");
    check(editor.pending_replacement_worksheet_names() ==
            replacement_names_before_failure,
        "failed combined save_as should preserve replacement sheet names");
    check(editor.pending_materialized_worksheet_names() ==
            materialized_names_before_failure,
        "failed combined save_as should preserve dirty materialized sheet names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), catalog_before_failure),
        "failed combined save_as should preserve planned worksheet catalog");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_failure),
        "failed combined save_as should preserve worksheet edit summaries");
    check(recovered_data.has_pending_changes(),
        "failed combined save_as should keep the borrowed WorksheetEditor dirty");
    check(recovered_data.cell_count() == materialized_cells_before_failure,
        "failed combined save_as should keep dirty sparse cells on the borrowed handle");
    check(!editor.last_edit_error().has_value(),
        "failed combined save_as should not create a public edit diagnostic");
    check(!std::filesystem::exists(missing_parent_output),
        "failed combined save_as should not create the missing-parent output");

    editor.save_as(safe_output);

    check(editor.pending_change_count() == pending_count_before_failure + 1,
        "recovered combined save_as should count the materialized worksheet flush");
    check(editor.pending_materialized_cell_count() == 0,
        "recovered combined save_as should clear dirty materialized aggregate diagnostics");
    check(editor.pending_materialized_worksheet_names().empty(),
        "recovered combined save_as should clear dirty materialized sheet names");
    check(!recovered_data.has_pending_changes(),
        "recovered combined save_as should clear the borrowed WorksheetEditor dirty flag");
    check(!editor.last_edit_error().has_value(),
        "recovered combined save_as should leave public edit diagnostics clear");

    const auto output_entries = fastxlsx::test::read_zip_entries(safe_output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string data_sheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string replaced_sheet_xml = output_entries.at("xl/worksheets/sheet2.xml");

    check_contains(workbook_xml, R"(name="RecoveredData")",
        "recovered combined save_as should persist the queued rename");
    check_not_contains(workbook_xml, R"(name="Data")",
        "recovered combined save_as should not resurrect the old sheet name");
    check_contains(data_sheet_xml, "dirty-before-failed-save",
        "recovered combined save_as should persist dirty materialized text");
    check_contains(data_sheet_xml, R"(<c r="C3"><v>123</v></c>)",
        "recovered combined save_as should persist dirty materialized number");
    check_contains(data_sheet_xml, R"(<dimension ref="A1:C3"/>)",
        "recovered combined save_as should refresh materialized worksheet dimension");
    check_contains(replaced_sheet_xml, "replacement-before-failed-save",
        "recovered combined save_as should persist the queued sheetData replacement");
    check_contains(replaced_sheet_xml, R"(<c r="B1"><v>88</v></c>)",
        "recovered combined save_as should persist the queued replacement number");
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "recovered combined save_as should persist memory-backed image bytes");
}

void test_docprops_are_preserved_through_patch()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_document_properties("fastxlsx-workbook-editor-docprops-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-docprops-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string core_before = source_entries.at("docProps/core.xml");
    const std::string app_before = source_entries.at("docProps/app.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(123.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("docProps/core.xml") == core_before,
        "patch save should preserve docProps/core.xml bytes");
    check(output_entries.at("docProps/app.xml") == app_before,
        "patch save should preserve docProps/app.xml bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<v>123</v>)",
        "patch save should still apply the requested workbook edit");
}

void test_rename_to_existing_name_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-dup-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-dup-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "renaming to an existing sheet name should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "untouched"); }),
        "renaming to an ASCII case-insensitive duplicate should throw FastXlsxError");

    // The editor must remain usable after a rejected rename.
    editor.rename_sheet("Data", "Renamed");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed")",
        "editor should still apply a valid rename after a rejected one");
}

void test_rename_missing_sheet_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-missing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Missing", "Renamed"); }),
        "renaming a missing sheet should throw FastXlsxError");

    // The editor must remain usable after a rejected rename.
    editor.rename_sheet("Data", "Renamed");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed")",
        "editor should still apply a valid rename after a missing-sheet rejection");
}

void test_rename_to_invalid_name_throws()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-invalid-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "renaming to a sheet name with invalid characters should throw FastXlsxError");
}

} // namespace

int main()
{
    try {
        test_pending_change_diagnostics_track_public_edits();
        test_last_edit_error_tracks_failed_public_edits();
        test_pending_worksheet_edit_summaries_track_public_facade_state();
        test_replace_sheet_data_source_read_failure_preserves_public_state();
        test_replace_sheet_data_source_xml_failures_preserve_public_state();
        test_replacement_guardrails_and_payload_diagnostics();
        test_missing_sheet_throws_and_editor_stays_usable();
        test_replace_sheet_data_failure_diagnostics_include_context();
        test_save_as_over_source_throws();
        test_noop_save_as_preserves_source_package_entries();
        test_noop_save_as_preserves_failed_edit_diagnostic();
        test_noop_save_as_preserves_failed_rename_diagnostic();
        test_noop_save_as_keeps_editor_usable_for_later_edits();
        test_failed_save_as_preserves_public_facade_state();
        test_successful_save_as_preserves_public_facade_state();
        test_empty_rows_emit_empty_sheet_data();
        test_text_uses_inline_strings_and_preserves_shared_strings();
        test_calc_metadata_requests_recalculation_without_inventing_calcchain();
        test_rename_sheet_changes_catalog_name_and_preserves_parts();
        test_replace_sheet_data_uses_planned_catalog_after_rename();
        test_rename_back_to_source_name_restores_public_diagnostics();
        test_rename_chain_back_to_source_name_clears_rename_only_summary();
        test_replacement_after_rename_chain_back_failure_uses_restored_name();
        test_failed_rename_preserves_pending_replacement_diagnostics();
        test_replace_image_updates_target_media_bytes_and_preserves_other_parts();
        test_replace_image_rejects_missing_or_mismatched_targets();
        test_replace_image_failure_diagnostics_include_context();
        test_replace_image_file_save_failure_preserves_pending_state();
        test_replace_image_file_crc_failure_preserves_pending_state();
        test_replace_image_same_part_later_replacement_wins();
        test_replace_image_memory_source_copies_bytes_before_save_as();
        test_public_workbook_editor_editing_end_to_end_smoke();
        test_public_workbook_editor_combined_failed_save_as_preserves_state();
        test_docprops_are_preserved_through_patch();
        test_rename_to_existing_name_throws_and_editor_stays_usable();
        test_rename_missing_sheet_throws_and_editor_stays_usable();
        test_rename_to_invalid_name_throws();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor facade check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor facade tests passed\n");
    return 0;
}
