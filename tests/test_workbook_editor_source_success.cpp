// WorkbookEditor source materialization success structure tests.
// Split from test_workbook_editor.cpp to keep the monolithic WorkbookEditor
// test source focused on the remaining core/public shards.

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
        || shard == "materialized";
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

void test_public_worksheet_editor_materializes_source_supported_values()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-supported-values-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-supported-values-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1"/>)"
          R"(<c r="B1" t="b"><v>1</v></c>)"
          R"(<c r="C1" t="b"><v>0</v></c>)"
          R"(<c r="D1" t="inlineStr"><is><t></t></is></c>)"
          R"(<c r="E1" t="inlineStr"><is/></c>)"
          R"(<c r="F1"><f t="array" ref="F1">SUM(B1:C1)</f><v>1</v></c>)"
          R"(<c r="G1"><f t="shared" si="0"/><v>7</v></c>)"
          R"(<c r="H1" ph="1"><v>8</v></c>)"
          R"(</row></sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> d1 = sheet.try_cell("D1");
    const std::optional<fastxlsx::CellValue> e1 = sheet.try_cell("E1");
    const std::optional<fastxlsx::CellValue> f1 = sheet.try_cell("F1");
    const std::optional<fastxlsx::CellValue> g1 = sheet.try_cell("G1");
    const std::optional<fastxlsx::CellValue> h1 = sheet.try_cell("H1");
    check(sheet.cell_count() == 8,
        "WorksheetEditor should count source blank and boolean cells as sparse records");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Blank,
        "WorksheetEditor should materialize self-closing source cells as explicit blank");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Boolean
            && b1->boolean_value(),
        "WorksheetEditor should materialize source boolean true");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Boolean
            && !c1->boolean_value(),
        "WorksheetEditor should materialize source boolean false");
    check(d1.has_value() && d1->kind() == fastxlsx::CellValueKind::Text
            && d1->text_value().empty(),
        "WorksheetEditor should materialize empty source inline text as empty text");
    check(e1.has_value() && e1->kind() == fastxlsx::CellValueKind::Blank,
        "WorksheetEditor should materialize inline string cells without text as blank");
    check(f1.has_value() && f1->kind() == fastxlsx::CellValueKind::Formula
            && f1->text_value() == "SUM(B1:C1)",
        "WorksheetEditor should flatten source formula metadata when formula text is present");
    check(g1.has_value() && g1->kind() == fastxlsx::CellValueKind::Number
            && g1->number_value() == 7.0,
        "WorksheetEditor should materialize cached values for metadata-only source formulas");
    check(h1.has_value() && h1->kind() == fastxlsx::CellValueKind::Number
            && h1->number_value() == 8.0,
        "WorksheetEditor should ignore source phonetic cell metadata");
    check(!sheet.has_pending_changes(),
        "read-only supported source value materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only supported source value materialization should not dirty WorkbookEditor");

    sheet.set_cell("I2", fastxlsx::CellValue::text("supported-values-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:I2"/>)",
        "flushed supported source values should contribute to projected dimension");
    check_contains(worksheet_xml, R"(<c r="A1"/>)",
        "source blank should be projected as an explicit blank cell");
    check_contains(worksheet_xml, R"(<c r="B1" t="b"><v>1</v></c>)",
        "source boolean true should be projected as a boolean cell");
    check_contains(worksheet_xml, R"(<c r="C1" t="b"><v>0</v></c>)",
        "source boolean false should be projected as a boolean cell");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t></t></is></c>)",
        "empty source inline text should remain an explicit empty text cell");
    check_contains(worksheet_xml, R"(<c r="E1"/>)",
        "source inline string without text should be projected as blank");
    check_contains(worksheet_xml, R"(<c r="F1"><f>SUM(B1:C1)</f></c>)",
        "source formula metadata should be projected as plain formula text");
    check_contains(worksheet_xml, R"(<c r="G1"><v>7</v></c>)",
        "metadata-only source formulas should be projected as cached scalar values");
    check_contains(worksheet_xml, R"(<c r="H1"><v>8</v></c>)",
        "source phonetic cell metadata should not be projected");
    check_contains(worksheet_xml,
        R"(<c r="I2" t="inlineStr"><is><t>supported-values-new-inline</t></is></c>)",
        "new WorksheetEditor text should continue to write inline strings");
    check_not_contains(worksheet_xml, R"(t="s")",
        "dirty supported source value projection should not introduce shared string indexes");
    check(output_entries.find("xl/sharedStrings.xml") == output_entries.end(),
        "dirty supported source value projection should not create a sharedStrings part");
    check_not_contains(output_entries.at("xl/_rels/workbook.xml.rels"),
        "relationships/sharedStrings",
        "dirty supported source value projection should not create a sharedStrings relationship");
    check_not_contains(output_entries.at("[Content_Types].xml"),
        "spreadsheetml.sharedStrings+xml",
        "dirty supported source value projection should not create a sharedStrings content type");
}

void test_public_worksheet_editor_materializes_source_scalar_string_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-string-type-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-string-type-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-source-string-type-dirty-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-string-type")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-string-type")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1" t="str"><v>plain &amp; &lt;text&gt;</v></c>)"
          R"(<c r="B1" t="str"><f>TEXT(C1,"@")&amp;"!"</f><v>cached &amp; stale</v></c>)"
          R"(<c r="C1"><v>7</v></c>)"
          R"(</row></sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), R"(t="str")",
        "source scalar-string fixture should contain t=str cells");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "plain & <text>",
        "WorksheetEditor should materialize source t=str scalar cells as text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Formula
            && b1->text_value() == "TEXT(C1,\"@\")&\"!\"",
        "WorksheetEditor should materialize source t=str formula cells as formulas");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Number
            && c1->number_value() == 7.0,
        "WorksheetEditor should still materialize numeric siblings beside t=str cells");
    check(!sheet.has_pending_changes(),
        "source t=str materialization should start as a clean read-only session");
    check(!editor.has_pending_changes(),
        "source t=str materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "source t=str materialization should not queue public Patch edits");

    editor.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source t=str materialization should copy source entries");

    sheet.set_cell("D2", fastxlsx::CellValue::text("string-type-new-inline"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "dirty source t=str projection should refresh dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>plain &amp; &lt;text&gt;</t></is></c>)",
        "dirty projection should write source t=str scalar text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1"><f>TEXT(C1,"@")&amp;"!"</f></c>)",
        "dirty projection should write source t=str formulas without cached values");
    check_not_contains(worksheet_xml, "cached &amp; stale",
        "dirty projection should not preserve stale source t=str formula cached values");
    check_not_contains(worksheet_xml, R"(t="str")",
        "dirty projection should not preserve source t=str cell type tokens");
    check_contains(worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>string-type-new-inline</t></is></c>)",
        "dirty projection should include later edits beside source t=str cells");
    check(output_entries.find("xl/sharedStrings.xml") == output_entries.end(),
        "dirty source t=str projection should not create a sharedStrings part");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-string-type",
        "dirty source t=str projection should preserve untouched sheets");
}

void test_public_worksheet_editor_flattens_source_inline_rich_text()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-inline-rich-text-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-inline-rich-text-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-inline-rich")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-inline-rich")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1" t="inlineStr"><is>)"
          R"(<r><rPr><b/><color rgb="FFFF0000"/></rPr><t>rich-</t></r>)"
          R"(<r><t>A&amp;B</t></r>)"
          R"(<r><t xml:space="preserve"> kept </t></r>)"
          R"(<rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh>)"
          R"(<phoneticPr fontId="1"/>)"
          R"(<extLst><ext uri="{fastxlsx-test}"><t>ignored-ext</t></ext></extLst>)"
          R"(</is></c>)"
          R"(</row></sheetData>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "rich-A&B kept ",
        "WorksheetEditor should flatten source inline rich text and ignore phonetic/ext text");
    check(!sheet.has_pending_changes(),
        "inline rich text materialization should start clean");
    check(!editor.has_pending_changes(),
        "inline rich text materialization should not dirty WorkbookEditor");

    sheet.set_cell("B2", fastxlsx::CellValue::text("inline-rich-new"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t xml:space="preserve">rich-A&amp;B kept </t></is></c>)",
        "dirty projection should write flattened source inline rich text as plain inline text");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>inline-rich-new</t></is></c>)",
        "dirty projection should include edits beside flattened inline rich text");
    check_not_contains(worksheet_xml, "<rPr>",
        "dirty projection should not preserve inline rich text formatting");
    check_not_contains(worksheet_xml, "<rPh",
        "dirty projection should not preserve inline phonetic markup");
    check_not_contains(worksheet_xml, "ignored-phonetic",
        "dirty projection should not keep ignored inline phonetic text");
    check_not_contains(worksheet_xml, "ignored-ext",
        "dirty projection should not keep ignored inline extension text");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-inline-rich",
        "dirty inline rich text projection should preserve untouched sheets");
}

void test_public_worksheet_editor_materializes_prefixed_source_inline_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-prefixed-inline-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-inline-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-inline-dirty-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("prefixed-inline-placeholder")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-prefixed-inline")});
        writer.close();
    }

    const std::string worksheet_xml =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test">)"
          R"(<x:sheetData>)"
          R"(<x:row r="1">)"
          R"(<x:c r="A1" t="inlineStr"><x:is><x:t>prefixed-inline</x:t></x:is></x:c>)"
          R"(<x:c r="B1" t="inlineStr"><x:is><x:t xml:space="preserve"> spaced </x:t></x:is></x:c>)"
          R"(<x:c r="C1" t="inlineStr"><x:is>)"
          R"(<x:r><x:rPr><x:b/></x:rPr><x:t>rich-</x:t></x:r>)"
          R"(<x:r><x:t>tail</x:t></x:r>)"
          R"(<x:rPh sb="1" eb="1"/><x:phoneticPr fontId="1"/><x:extLst/>)"
          R"(<x:rPh sb="0" eb="1"><fx:opaque><x:r><x:t>ignored-nested-phonetic</x:t></x:r></fx:opaque></x:rPh>)"
          R"(<x:extLst><x:ext uri="{fastxlsx-test}"><fx:opaque><x:r><x:t>ignored-nested-ext</x:t></x:r></fx:opaque></x:ext></x:extLst>)"
          R"(</x:is></x:c>)"
          R"(</x:row>)"
          R"(<x:row r="2">)"
          R"(<x:c r="A2"><x:v>42</x:v></x:c>)"
          R"(<x:c r="B2" t="b"><x:v>1</x:v></x:c>)"
          R"(<x:c r="C2"><x:f>SUM(A2:A2)</x:f><x:v>999</x:v></x:c>)"
          R"(</x:row>)"
          R"(</x:sheetData>)"
          R"(</x:worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<x:worksheet",
        "prefixed inline fixture should use a qualified worksheet root");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<x:is>",
        "prefixed inline fixture should use qualified inline-string wrappers");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "ignored-nested-ext",
        "prefixed inline fixture should carry nested ignored extension text");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<x:extLst/>",
        "prefixed inline fixture should carry self-closing ignored metadata");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    const std::optional<fastxlsx::CellValue> b2 = sheet.try_cell("B2");
    const std::optional<fastxlsx::CellValue> c2 = sheet.try_cell("C2");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "prefixed-inline",
        "WorksheetEditor should materialize prefixed source inline text by local-name");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == " spaced ",
        "WorksheetEditor should keep xml:space text from prefixed inline wrappers");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Text
            && c1->text_value() == "rich-tail",
        "WorksheetEditor should flatten prefixed source inline rich text by local-name");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Number
            && a2->number_value() == 42.0,
        "WorksheetEditor should materialize prefixed numeric value wrappers");
    check(b2.has_value() && b2->kind() == fastxlsx::CellValueKind::Boolean
            && b2->boolean_value(),
        "WorksheetEditor should materialize prefixed boolean value wrappers");
    check(c2.has_value() && c2->kind() == fastxlsx::CellValueKind::Formula
            && c2->text_value() == "SUM(A2:A2)",
        "WorksheetEditor should materialize prefixed formula wrappers and ignore cached values");
    check(!sheet.has_pending_changes(),
        "prefixed inline materialization should start clean");
    check(!editor.has_pending_changes(),
        "prefixed inline materialization should not dirty WorkbookEditor");

    editor.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after prefixed inline materialization should copy source entries");

    sheet.set_cell("D2", fastxlsx::CellValue::text("prefixed-inline-dirty"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string dirty_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(dirty_worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "dirty prefixed inline projection should refresh the sparse-store dimension");
    check_contains(dirty_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>prefixed-inline</t></is></c>)",
        "dirty projection should write prefixed source inline text as plain inlineStr");
    check_contains(dirty_worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t xml:space="preserve"> spaced </t></is></c>)",
        "dirty projection should preserve prefixed inline whitespace in plain inlineStr");
    check_contains(dirty_worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>rich-tail</t></is></c>)",
        "dirty projection should write flattened prefixed inline rich text as plain text");
    check_contains(dirty_worksheet_xml, R"(<c r="A2"><v>42</v></c>)",
        "dirty projection should preserve materialized numeric values");
    check_contains(dirty_worksheet_xml, R"(<c r="B2" t="b"><v>1</v></c>)",
        "dirty projection should preserve materialized boolean values");
    check_contains(dirty_worksheet_xml, R"(<c r="C2"><f>SUM(A2:A2)</f></c>)",
        "dirty projection should preserve formulas without stale cached values");
    check_contains(dirty_worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>prefixed-inline-dirty</t></is></c>)",
        "dirty projection should include edits beside prefixed source cells");
    check_not_contains(dirty_worksheet_xml, "<x:",
        "dirty full-worksheet projection should not preserve source element prefixes");
    check_not_contains(dirty_worksheet_xml, "ignored-phonetic",
        "dirty projection should not keep ignored prefixed phonetic text");
    check_not_contains(dirty_worksheet_xml, "ignored-nested-phonetic",
        "dirty projection should not keep nested ignored prefixed phonetic text");
    check_not_contains(dirty_worksheet_xml, "ignored-nested-ext",
        "dirty projection should not keep nested ignored prefixed extension text");
    check_not_contains(dirty_worksheet_xml, "ignored-ext",
        "dirty projection should not keep ignored prefixed extension text");
    check_not_contains(dirty_worksheet_xml, "<v>999</v>",
        "dirty projection should not preserve stale cached formula values");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty prefixed inline projection should preserve untouched sheets");
}

void test_public_worksheet_editor_materializes_source_default_style_attribute_as_unstyled()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-default-style-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-default-style-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("loadable-before-style"),
            fastxlsx::CellView::text("explicit-default-source-style"),
            fastxlsx::CellView::text("single-quoted-default-source-style"),
            fastxlsx::CellView::text("spaced-default-source-style")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-explicit-default")});
        writer.close();
    }

    std::map<std::string, std::string> entries =
        fastxlsx::test::read_zip_entries(source);
    std::string& source_worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    replace_first_or_throw(source_worksheet_xml,
        R"(<c r="B1" t="inlineStr">)",
        R"(<c r="B1" s="0" t="inlineStr">)");
    replace_first_or_throw(source_worksheet_xml,
        R"(<c r="C1" t="inlineStr">)",
        R"(<c r="C1" s='0' t="inlineStr">)");
    replace_first_or_throw(source_worksheet_xml,
        R"(<c r="D1" t="inlineStr">)",
        R"(<c r="D1" s = "0" t="inlineStr">)");
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), R"(s="0")",
        "source default-style fixture should contain an explicit s=0 attribute");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), R"(s='0')",
        "source default-style fixture should contain a single-quoted explicit s=0 attribute");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), R"(s = "0")",
        "source default-style fixture should contain an explicit s=0 attribute with whitespace around equals");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> d1 = sheet.try_cell("D1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "loadable-before-style" && !a1->has_style(),
        "WorksheetEditor should materialize the unstyled source cell");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "explicit-default-source-style" && !b1->has_style(),
        "WorksheetEditor should normalize source s=0 to no style handle");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Text
            && c1->text_value() == "single-quoted-default-source-style"
            && !c1->has_style(),
        "WorksheetEditor should normalize source single-quoted s=0 to no style handle");
    check(d1.has_value() && d1->kind() == fastxlsx::CellValueKind::Text
            && d1->text_value() == "spaced-default-source-style" && !d1->has_style(),
        "WorksheetEditor should normalize source s=0 with whitespace around equals to no style handle");
    check(!sheet.has_pending_changes(),
        "source s=0 materialization should start as a clean read-only session");
    check(!editor.has_pending_changes(),
        "source s=0 materialization should not dirty WorkbookEditor");

    sheet.set_cell("E1", fastxlsx::CellValue::text("dirty-default-style-trigger"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>loadable-before-style</t></is></c>)",
        "dirty projection should keep the prior unstyled source cell");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>explicit-default-source-style</t></is></c>)",
        "dirty projection should write source s=0 as an unstyled inline string");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>single-quoted-default-source-style</t></is></c>)",
        "dirty projection should write source single-quoted s=0 as an unstyled inline string");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t>spaced-default-source-style</t></is></c>)",
        "dirty projection should write source s=0 with whitespace around equals as an unstyled inline string");
    check_contains(worksheet_xml,
        R"(<c r="E1" t="inlineStr"><is><t>dirty-default-style-trigger</t></is></c>)",
        "dirty projection should include the trigger edit");
    check_not_contains(worksheet_xml, R"(s="0")",
        "dirty projection should not serialize the normalized default style attribute");
    check_not_contains(worksheet_xml, R"(s='0')",
        "dirty projection should not serialize the normalized single-quoted default style attribute");
    check_not_contains(worksheet_xml, R"(s = "0")",
        "dirty projection should not serialize the normalized whitespace-around-equals default style attribute");
}

void test_public_worksheet_editor_defers_source_shared_strings_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-dirty-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-missing-target-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(2.0),
            fastxlsx::CellView::boolean(true),
            fastxlsx::CellView::formula("A1+1")});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("requires-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    check(entries.find("xl/sharedStrings.xml") != entries.end(),
        "lazy sharedStrings fixture should contain a sharedStrings part for the second sheet");
    check_not_contains(entries.at("xl/worksheets/sheet1.xml"), R"(t="s")",
        "lazy sharedStrings fixture Data sheet should not contain shared string indexes");
    check_contains(entries.at("xl/worksheets/sheet2.xml"), R"(t="s")",
        "lazy sharedStrings fixture Shared sheet should contain shared string indexes");
    const std::string shared_strings_before = entries.at("xl/sharedStrings.xml");

    std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
    replace_first_or_throw(workbook_relationships,
        R"(Target="sharedStrings.xml")",
        R"(Target="missingSharedStrings.xml")");
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 2.0,
        "WorksheetEditor should materialize non-shared-string numbers without loading sharedStrings");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Boolean
            && b1->boolean_value(),
        "WorksheetEditor should materialize non-shared-string booleans without loading sharedStrings");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Formula
            && c1->text_value() == "A1+1",
        "WorksheetEditor should materialize formulas without loading sharedStrings");
    check(!sheet.has_pending_changes(),
        "lazy sharedStrings non-index materialization should start clean");
    check(!editor.has_pending_changes(),
        "lazy sharedStrings non-index materialization should not dirty the editor");

    sheet.set_cell("D1", fastxlsx::CellValue::text("inline-after-lazy-sharedStrings"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t>inline-after-lazy-sharedStrings</t></is></c>)",
        "dirty lazy sharedStrings projection should still write new text as inlineStr");
    check_not_contains(worksheet_xml, R"(t="s")",
        "dirty lazy sharedStrings projection should not introduce shared string indexes");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty lazy sharedStrings projection should preserve the source sharedStrings bytes");
    check_contains(output_entries.at("xl/_rels/workbook.xml.rels"),
        R"(Target="missingSharedStrings.xml")",
        "dirty lazy sharedStrings projection should not repair the stale workbook relationship");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy sharedStrings materialization should not mutate the source package");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "workbook sharedStrings relationship targets an unknown package part",
        "usable-after-lazy-missing-sharedstrings-target",
        "lazy missing sharedStrings target",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_defers_duplicate_shared_strings_relationship_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(7.0)});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("duplicate-rel-needs-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
    replace_first_or_throw(workbook_relationships,
        R"(</Relationships>)",
        R"(<Relationship Id="rId99" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(</Relationships>)");
    const std::string shared_strings_before = entries.at("xl/sharedStrings.xml");
    write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 7.0,
        "duplicate sharedStrings relationships should not block a non-index sheet");
    check(!editor.has_pending_changes(),
        "lazy duplicate sharedStrings relationship read should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("after-duplicate-rel-lazy-load"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty lazy duplicate-rel projection should preserve source sharedStrings bytes");
    check_contains(output_entries.at("xl/_rels/workbook.xml.rels"), R"(Id="rId99")",
        "dirty lazy duplicate-rel projection should preserve duplicate relationship bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>after-duplicate-rel-lazy-load</t></is></c>)",
        "dirty lazy duplicate-rel projection should still write new text as inlineStr");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "workbook sharedStrings lookup found multiple sharedStrings relationships",
        "usable-after-lazy-duplicate-sharedstrings-relationship",
        "lazy duplicate sharedStrings relationship",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_defers_malformed_shared_strings_xml_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(11.0)});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("malformed-xml-needs-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/sharedStrings.xml") = R"(<notSst/>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 11.0,
        "malformed sharedStrings XML should not block a non-index sheet");
    check(!editor.has_pending_changes(),
        "lazy malformed sharedStrings XML read should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("after-malformed-xml-lazy-load"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == R"(<notSst/>)",
        "dirty lazy malformed-xml projection should preserve malformed sharedStrings bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>after-malformed-xml-lazy-load</t></is></c>)",
        "dirty lazy malformed-xml projection should still write new text as inlineStr");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy malformed sharedStrings XML materialization should not mutate the source package");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "CellStore sharedStrings loader root is missing an sst element",
        "usable-after-lazy-malformed-sharedstrings",
        "lazy malformed sharedStrings XML",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_defers_wrong_shared_strings_content_type_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(13.0)});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("wrong-content-type-needs-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    std::string& content_types = entries.at("[Content_Types].xml");
    replace_first_or_throw(content_types,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    const std::string shared_strings_before = entries.at("xl/sharedStrings.xml");
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 13.0,
        "wrong sharedStrings content type should not block a non-index sheet");
    check(!editor.has_pending_changes(),
        "lazy wrong sharedStrings content type read should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("after-wrong-content-type-lazy-load"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty lazy wrong-content-type projection should preserve sharedStrings bytes");
    check_contains(output_entries.at("[Content_Types].xml"),
        R"(PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")",
        "dirty lazy wrong-content-type projection should preserve wrong content type metadata");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>after-wrong-content-type-lazy-load</t></is></c>)",
        "dirty lazy wrong-content-type projection should still write new text as inlineStr");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy wrong sharedStrings content type materialization should not mutate the source package");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "workbook sharedStrings relationship target is not a sharedStrings part",
        "usable-after-lazy-wrong-sharedstrings-content-type",
        "lazy wrong sharedStrings content type",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_materializes_source_max_coordinate_and_erases_edge()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-source-max-coordinate-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-erase-output.xlsx");

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    check(entries.find("xl/sharedStrings.xml") == entries.end(),
        "supported source values fixture should not require a sharedStrings part");
    check_not_contains(entries.at("xl/_rels/workbook.xml.rels"),
        "relationships/sharedStrings",
        "supported source values fixture should not require a sharedStrings relationship");
    check_not_contains(entries.at("[Content_Types].xml"),
        "spreadsheetml.sharedStrings+xml",
        "supported source values fixture should not require a sharedStrings content type");
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-max-a1</t></is></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="inlineStr"><is><t>source-max-a2</t></is></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="inlineStr"><is><t>source-max-edge</t></is></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                by_position.text_value() == "source-max-edge",
            "source max-coordinate materialization should read through row/column overloads");
        check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                by_a1.text_value() == "source-max-edge",
            "source max-coordinate materialization should read through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "source-max-edge",
                "source max-coordinate range snapshot should preserve source text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate materialization should copy source entries");

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "source-max-edge",
        "source max-coordinate erase output should omit the erased edge text");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-max-a1</t></is></c>)",
        "source max-coordinate erase output should preserve source A1");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-max-a2</t></is></c>)",
        "source max-coordinate erase output should preserve source A2");
    check_contains(erase_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "source max-coordinate erase output should preserve untouched sheets");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "source-max-edge",
        "source max-coordinate erase should not mutate the source package bytes");
}

void test_public_worksheet_editor_materializes_source_max_coordinate_formula_and_erases_edge()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-source-max-coordinate-formula-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-formula-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-formula-erase-output.xlsx");

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-formula-a1</t></is></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="inlineStr"><is><t>source-formula-a2</t></is></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576"><f>SUM(A1:B1)&amp;"&lt;source-edge&gt;"</f><v>12345</v></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<v>12345</v>",
        "source max-coordinate formula fixture should contain a stale cached value");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate formula materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate formula materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate formula materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate formula materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate formula materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate formula materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Formula &&
                by_position.text_value() == R"(SUM(A1:B1)&"<source-edge>")",
            "source max-coordinate formula materialization should ignore stale cached scalar values");
        check(by_a1.kind() == fastxlsx::CellValueKind::Formula &&
                by_a1.text_value() == R"(SUM(A1:B1)&"<source-edge>")",
            "source max-coordinate formula materialization should read formulas through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate formula range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate formula range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    edge_cells[0].value.text_value() == R"(SUM(A1:B1)&"<source-edge>")",
                "source max-coordinate formula range snapshot should preserve source formula text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate formula materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate formula materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate formula materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate formula materialization should copy source entries");

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate formula erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate formula erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate formula erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate formula erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate formula get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate formula range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate formula erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate formula erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate formula erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate formula erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate formula erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate formula erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate formula erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate formula erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate formula erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate formula erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate formula erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate formula erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate formula erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate formula erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "SUM(A1:B1)",
        "source max-coordinate formula erase output should omit the erased edge formula");
    check_not_contains(worksheet_xml, "12345",
        "source max-coordinate formula erase output should omit the stale cached scalar value");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-formula-a1</t></is></c>)",
        "source max-coordinate formula erase output should preserve source A1");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate formula erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-formula-a2</t></is></c>)",
        "source max-coordinate formula erase output should preserve source A2");
    check_contains(erase_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "source max-coordinate formula erase output should preserve untouched sheets");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<v>12345</v>",
        "source max-coordinate formula erase should not mutate the source package bytes");
}

void test_public_worksheet_editor_materializes_source_max_coordinate_shared_string_and_erases_edge()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-erase-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions writer_options;
        writer_options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, writer_options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("source-shared-a1"),
            fastxlsx::CellView::number(1.0),
            fastxlsx::CellView::text("source-shared-edge & <max>")});
        data.append_row({fastxlsx::CellView::text("source-shared-a2")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-shared-edge")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/sharedStrings.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4" uniqueCount="4">)"
          R"(<si><t>source-shared-a1</t></si>)"
          R"(<si><t>source-shared-edge &amp; &lt;max&gt;</t></si>)"
          R"(<si><t>source-shared-a2</t></si>)"
          R"(<si><t>keep-shared-edge</t></si>)"
          R"(</sst>)";
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="s"><v>0</v></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="s"><v>2</v></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="s"><v>1</v></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before =
        source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, "source-shared-edge &amp; &lt;max&gt;",
        "source max-coordinate shared string fixture should contain the edge text");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate shared string fixture should store the edge cell as t=s");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate shared string materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate shared string materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate shared string materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate shared string materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate shared string materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate shared string materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                by_position.text_value() == "source-shared-edge & <max>",
            "source max-coordinate shared string materialization should decode XML entities");
        check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                by_a1.text_value() == "source-shared-edge & <max>",
            "source max-coordinate shared string materialization should read text through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate shared string range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate shared string range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "source-shared-edge & <max>",
                "source max-coordinate shared string range snapshot should preserve source text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate shared string materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate shared string materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate shared string materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate shared string materialization should copy source entries");

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate shared string erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate shared string erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate shared string erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate shared string erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate shared string get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate shared string range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate shared string erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate shared string erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate shared string erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate shared string erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate shared string erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate shared string erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate shared string erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate shared string erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate shared string erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate shared string erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate shared string erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate shared string erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate shared string erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate shared string erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "source-shared-edge",
        "source max-coordinate shared string erase output should omit the erased edge text");
    check_not_contains(worksheet_xml, R"(t="s")",
        "source max-coordinate shared string erase output should flush remaining text as inline strings");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-shared-a1</t></is></c>)",
        "source max-coordinate shared string erase output should preserve source A1 as inline text");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate shared string erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-shared-a2</t></is></c>)",
        "source max-coordinate shared string erase output should preserve source A2 as inline text");
    check(erase_entries.find("xl/sharedStrings.xml") != erase_entries.end()
            && erase_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "source max-coordinate shared string erase output should preserve source sharedStrings bytes");
    check(erase_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "source max-coordinate shared string erase output should preserve untouched sheets byte-for-byte");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate shared string erase should not mutate the source package bytes");
}

void test_public_worksheet_editor_materializes_source_max_coordinate_scalar_values_and_erases_edge()
{
    struct SourceMaxCoordinateScalarCase {
        std::string_view name;
        std::string_view edge_cell_xml;
        fastxlsx::CellValueKind expected_kind;
        double expected_number = 0.0;
        bool expected_boolean = false;
        std::string_view absent_payload;
    };

    const std::array<SourceMaxCoordinateScalarCase, 3> cases {{
        {"number",
            R"(<c r="XFD1048576"><v>9000.25</v></c>)",
            fastxlsx::CellValueKind::Number,
            9000.25,
            false,
            "9000.25"},
        {"boolean-false",
            R"(<c r="XFD1048576" t="b"><v>0</v></c>)",
            fastxlsx::CellValueKind::Boolean,
            0.0,
            false,
            R"(t="b")"},
        {"blank",
            R"(<c r="XFD1048576"/>)",
            fastxlsx::CellValueKind::Blank,
            0.0,
            false,
            R"(XFD1048576)"},
    }};

    for (const SourceMaxCoordinateScalarCase& case_info : cases) {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-source.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-noop-output.xlsx");
        const std::filesystem::path erase_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-erase-output.xlsx");

        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("placeholder")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-scalar-edge")});
            writer.close();
        }

        std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
        std::string worksheet_xml =
            std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
              R"(<dimension ref="A1:XFD1048576"/>)"
              R"(<sheetData>)"
              R"(<row r="1">)"
              R"(<c r="A1" t="inlineStr"><is><t>source-scalar-a1</t></is></c>)"
              R"(<c r="B1"><v>1</v></c>)"
              R"(</row>)"
              R"(<row r="2">)"
              R"(<c r="A2" t="inlineStr"><is><t>source-scalar-a2</t></is></c>)"
              R"(</row>)"
              R"(<row r="1048576">)";
        worksheet_xml.append(case_info.edge_cell_xml.data(), case_info.edge_cell_xml.size());
        worksheet_xml += R"(</row></sheetData></worksheet>)";
        entries.at("xl/worksheets/sheet1.xml") = worksheet_xml;
        write_stored_zip_entries(source, entries);
        const auto source_entries = fastxlsx::test::read_zip_entries(source);
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate scalar fixture should contain the edge cell");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

        check(sheet.cell_count() == 4,
            "source max-coordinate scalar materialization should load sparse source records only");
        check(!sheet.has_pending_changes(),
            "read-only source max-coordinate scalar materialization should start clean");
        check(!editor.has_pending_changes(),
            "read-only source max-coordinate scalar materialization should not dirty WorkbookEditor");
        check(editor.pending_change_count() == 0,
            "read-only source max-coordinate scalar materialization should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only source max-coordinate scalar materialization should not expose dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only source max-coordinate scalar materialization should not expose dirty cell count");

        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        if (case_info.expected_kind == fastxlsx::CellValueKind::Number) {
            check(by_position.kind() == fastxlsx::CellValueKind::Number &&
                    by_position.number_value() == case_info.expected_number,
                "source max-coordinate number should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Number &&
                    by_a1.number_value() == case_info.expected_number,
                "source max-coordinate number should materialize through A1 overloads");
        } else if (case_info.expected_kind == fastxlsx::CellValueKind::Boolean) {
            check(by_position.kind() == fastxlsx::CellValueKind::Boolean &&
                    by_position.boolean_value() == case_info.expected_boolean,
                "source max-coordinate boolean should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Boolean &&
                    by_a1.boolean_value() == case_info.expected_boolean,
                "source max-coordinate boolean should materialize through A1 overloads");
        } else {
            check(by_position.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate blank should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate blank should materialize through A1 overloads");
        }
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.size() == 1,
                "source max-coordinate scalar range snapshot should expose the edge record");
            if (edge_cells.size() == 1) {
                check(edge_cells[0].reference.row == 1048576 &&
                        edge_cells[0].reference.column == 16384,
                    "source max-coordinate scalar range snapshot should preserve legal boundary coordinates");
                check(edge_cells[0].value.kind() == case_info.expected_kind,
                    "source max-coordinate scalar range snapshot should preserve the source value kind");
            }
        }

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "no-op save_as after source max-coordinate scalar materialization should keep the handle clean");
        check(!editor.has_pending_changes(),
            "no-op save_as after source max-coordinate scalar materialization should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "no-op save_as after source max-coordinate scalar materialization should not create public edits");
        check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
            "no-op save_as after source max-coordinate scalar materialization should copy source entries");

        sheet.erase_cell("XFD1048576");
        check(!editor.last_edit_error().has_value(),
            "source max-coordinate scalar erase should not create edit diagnostics");
        check(sheet.has_pending_changes(),
            "source max-coordinate scalar erase should dirty the materialized handle");
        check(sheet.cell_count() == 3,
            "source max-coordinate scalar erase should shrink the sparse record count");
        check(!sheet.try_cell(1048576, 16384).has_value(),
            "source max-coordinate scalar erase should remove row/column readback");
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell("XFD1048576");
        }), "source max-coordinate scalar get_cell should throw after erase");
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.empty(),
                "source max-coordinate scalar range snapshot should be empty after erase");
        }
        {
            const std::vector<std::string> names =
                editor.pending_materialized_worksheet_names();
            check(names.size() == 1 && names[0] == "Data",
                "source max-coordinate scalar erase dirty diagnostics should use source sheet name");
        }
        check(editor.pending_materialized_cell_count() == 3,
            "source max-coordinate scalar erase dirty diagnostics should report remaining sparse records");

        editor.save_as(erase_output);
        check(!sheet.has_pending_changes(),
            "save_as after source max-coordinate scalar erase should clean the handle");
        check(editor.pending_change_count() == 1,
            "save_as after source max-coordinate scalar erase should count one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "save_as after source max-coordinate scalar erase should clear dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "save_as after source max-coordinate scalar erase should clear dirty cell count");
        check(editor.pending_worksheet_edits().empty(),
            "save_as after source max-coordinate scalar erase should clear summaries");

        const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
        const std::string erase_worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
        check_contains(erase_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "source max-coordinate scalar erase output should shrink dimension to remaining source records");
        check_not_contains(erase_worksheet_xml, "XFD1048576",
            "source max-coordinate scalar erase output should omit the erased edge reference");
        if (case_info.name != "blank") {
            check_not_contains(erase_worksheet_xml, case_info.absent_payload,
                "source max-coordinate scalar erase output should omit the erased edge payload");
        }
        check_contains(erase_worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>source-scalar-a1</t></is></c>)",
            "source max-coordinate scalar erase output should preserve source A1");
        check_contains(erase_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "source max-coordinate scalar erase output should preserve source B1");
        check_contains(erase_worksheet_xml,
            R"(<c r="A2" t="inlineStr"><is><t>source-scalar-a2</t></is></c>)",
            "source max-coordinate scalar erase output should preserve source A2");
        check(erase_entries.at("xl/worksheets/sheet2.xml") ==
                source_entries.at("xl/worksheets/sheet2.xml"),
            "source max-coordinate scalar erase output should preserve untouched sheets byte-for-byte");
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate scalar erase should not mutate the source package bytes");
    }
}

void test_public_worksheet_editor_materializes_source_max_coordinate_empty_inline_strings_and_erases_edge()
{
    struct SourceMaxCoordinateInlineCase {
        std::string_view name;
        std::string_view edge_cell_xml;
        fastxlsx::CellValueKind expected_kind;
        std::string_view absent_payload;
    };

    const std::array<SourceMaxCoordinateInlineCase, 2> cases {{
        {"empty-text",
            R"(<c r="XFD1048576" t="inlineStr"><is><t></t></is></c>)",
            fastxlsx::CellValueKind::Text,
            R"(<t></t>)"},
        {"inline-without-text",
            R"(<c r="XFD1048576" t="inlineStr"><is/></c>)",
            fastxlsx::CellValueKind::Blank,
            R"(<is/>)"},
    }};

    for (const SourceMaxCoordinateInlineCase& case_info : cases) {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-source.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-noop-output.xlsx");
        const std::filesystem::path erase_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-erase-output.xlsx");

        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("placeholder")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-empty-inline-edge")});
            writer.close();
        }

        std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
        std::string worksheet_xml =
            std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
              R"(<dimension ref="A1:XFD1048576"/>)"
              R"(<sheetData>)"
              R"(<row r="1">)"
              R"(<c r="A1" t="inlineStr"><is><t>source-empty-inline-a1</t></is></c>)"
              R"(<c r="B1"><v>1</v></c>)"
              R"(</row>)"
              R"(<row r="2">)"
              R"(<c r="A2" t="inlineStr"><is><t>source-empty-inline-a2</t></is></c>)"
              R"(</row>)"
              R"(<row r="1048576">)";
        worksheet_xml.append(case_info.edge_cell_xml.data(), case_info.edge_cell_xml.size());
        worksheet_xml += R"(</row></sheetData></worksheet>)";
        entries.at("xl/worksheets/sheet1.xml") = worksheet_xml;
        write_stored_zip_entries(source, entries);
        const auto source_entries = fastxlsx::test::read_zip_entries(source);
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate empty inline fixture should contain the edge cell");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

        check(sheet.cell_count() == 4,
            "source max-coordinate empty inline materialization should load sparse source records only");
        check(!sheet.has_pending_changes(),
            "read-only source max-coordinate empty inline materialization should start clean");
        check(!editor.has_pending_changes(),
            "read-only source max-coordinate empty inline materialization should not dirty WorkbookEditor");
        check(editor.pending_change_count() == 0,
            "read-only source max-coordinate empty inline materialization should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only source max-coordinate empty inline materialization should not expose dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only source max-coordinate empty inline materialization should not expose dirty cell count");

        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        if (case_info.expected_kind == fastxlsx::CellValueKind::Text) {
            check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                    by_position.text_value().empty(),
                "source max-coordinate empty inline text should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                    by_a1.text_value().empty(),
                "source max-coordinate empty inline text should materialize through A1 overloads");
        } else {
            check(by_position.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate inline string without text should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate inline string without text should materialize through A1 overloads");
        }
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.size() == 1,
                "source max-coordinate empty inline range snapshot should expose the edge record");
            if (edge_cells.size() == 1) {
                check(edge_cells[0].reference.row == 1048576 &&
                        edge_cells[0].reference.column == 16384,
                    "source max-coordinate empty inline range snapshot should preserve legal boundary coordinates");
                check(edge_cells[0].value.kind() == case_info.expected_kind,
                    "source max-coordinate empty inline range snapshot should preserve the source value kind");
                if (case_info.expected_kind == fastxlsx::CellValueKind::Text) {
                    check(edge_cells[0].value.text_value().empty(),
                        "source max-coordinate empty inline range snapshot should preserve empty text");
                }
            }
        }

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "no-op save_as after source max-coordinate empty inline materialization should keep the handle clean");
        check(!editor.has_pending_changes(),
            "no-op save_as after source max-coordinate empty inline materialization should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "no-op save_as after source max-coordinate empty inline materialization should not create public edits");
        check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
            "no-op save_as after source max-coordinate empty inline materialization should copy source entries");

        sheet.erase_cell("XFD1048576");
        check(!editor.last_edit_error().has_value(),
            "source max-coordinate empty inline erase should not create edit diagnostics");
        check(sheet.has_pending_changes(),
            "source max-coordinate empty inline erase should dirty the materialized handle");
        check(sheet.cell_count() == 3,
            "source max-coordinate empty inline erase should shrink the sparse record count");
        check(!sheet.try_cell(1048576, 16384).has_value(),
            "source max-coordinate empty inline erase should remove row/column readback");
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell("XFD1048576");
        }), "source max-coordinate empty inline get_cell should throw after erase");
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.empty(),
                "source max-coordinate empty inline range snapshot should be empty after erase");
        }
        {
            const std::vector<std::string> names =
                editor.pending_materialized_worksheet_names();
            check(names.size() == 1 && names[0] == "Data",
                "source max-coordinate empty inline erase dirty diagnostics should use source sheet name");
        }
        check(editor.pending_materialized_cell_count() == 3,
            "source max-coordinate empty inline erase dirty diagnostics should report remaining sparse records");

        editor.save_as(erase_output);
        check(!sheet.has_pending_changes(),
            "save_as after source max-coordinate empty inline erase should clean the handle");
        check(editor.pending_change_count() == 1,
            "save_as after source max-coordinate empty inline erase should count one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "save_as after source max-coordinate empty inline erase should clear dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "save_as after source max-coordinate empty inline erase should clear dirty cell count");
        check(editor.pending_worksheet_edits().empty(),
            "save_as after source max-coordinate empty inline erase should clear summaries");

        const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
        const std::string erase_worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
        check_contains(erase_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "source max-coordinate empty inline erase output should shrink dimension to remaining source records");
        check_not_contains(erase_worksheet_xml, "XFD1048576",
            "source max-coordinate empty inline erase output should omit the erased edge reference");
        check_not_contains(erase_worksheet_xml, case_info.absent_payload,
            "source max-coordinate empty inline erase output should omit the erased edge payload");
        check_contains(erase_worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>source-empty-inline-a1</t></is></c>)",
            "source max-coordinate empty inline erase output should preserve source A1");
        check_contains(erase_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "source max-coordinate empty inline erase output should preserve source B1");
        check_contains(erase_worksheet_xml,
            R"(<c r="A2" t="inlineStr"><is><t>source-empty-inline-a2</t></is></c>)",
            "source max-coordinate empty inline erase output should preserve source A2");
        check(erase_entries.at("xl/worksheets/sheet2.xml") ==
                source_entries.at("xl/worksheets/sheet2.xml"),
            "source max-coordinate empty inline erase output should preserve untouched sheets byte-for-byte");
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate empty inline erase should not mutate the source package bytes");
    }
}

void test_public_worksheet_editor_materializes_source_max_coordinate_rich_shared_string_and_erases_edge()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-erase-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::number(7.0)});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    const std::string rich_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="3" uniqueCount="3">)"
        R"(<si><t>source-rich-a1</t></si>)"
        R"(<si><r><t>rich-</t></r><r><t>A&amp;B </t></r><r><t>&lt;edge&gt;</t></r><rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh><phoneticPr fontId="1"/><extLst><ext uri="{fastxlsx-test}"><t>ignored-ext</t></ext></extLst></si>)"
        R"(<si><t>source-rich-a2</t></si>)"
        R"(</sst>)";
    entries.at("xl/sharedStrings.xml") = rich_shared_strings;
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="s"><v>0</v></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="s"><v>2</v></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="s"><v>1</v></c>)"
          R"(</row></sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, R"(<rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh>)",
        "source rich shared string fixture should contain ignored phonetic text");
    check_contains(shared_strings_before, R"(<ext uri="{fastxlsx-test}"><t>ignored-ext</t></ext>)",
        "source rich shared string fixture should contain ignored extension text");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate rich shared string fixture should store the edge as t=s");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate rich shared string materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate rich shared string materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate rich shared string materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate rich shared string materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate rich shared string materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate rich shared string materialization should not expose dirty cell count");

    const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
    const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
    check(by_position.kind() == fastxlsx::CellValueKind::Text &&
            by_position.text_value() == "rich-A&B <edge>",
        "source max-coordinate rich shared string should flatten runs through row/column overloads");
    check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
            by_a1.text_value() == "rich-A&B <edge>",
        "source max-coordinate rich shared string should flatten runs through A1 overloads");
    check(sheet.get_cell("A1").text_value() == "source-rich-a1",
        "source rich shared string fixture should materialize A1 beside the edge");
    check(sheet.get_cell("A2").text_value() == "source-rich-a2",
        "source rich shared string fixture should materialize A2 beside the edge");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate rich shared string range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate rich shared string range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "rich-A&B <edge>",
                "source max-coordinate rich shared string range snapshot should preserve flattened text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate rich shared string materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate rich shared string materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate rich shared string materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate rich shared string materialization should copy source entries");

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate rich shared string erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate rich shared string erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate rich shared string erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate rich shared string erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate rich shared string get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate rich shared string range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate rich shared string erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate rich shared string erase dirty diagnostics should report remaining sparse records");

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate rich shared string erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate rich shared string erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate rich shared string erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate rich shared string erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate rich shared string erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string erase_worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(erase_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate rich shared string erase output should shrink dimension to remaining source records");
    check_not_contains(erase_worksheet_xml, "XFD1048576",
        "source max-coordinate rich shared string erase output should omit the erased edge reference");
    check_not_contains(erase_worksheet_xml, "rich-A&amp;B",
        "source max-coordinate rich shared string erase output should omit the erased flattened text");
    check_contains(erase_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-rich-a1</t></is></c>)",
        "source max-coordinate rich shared string erase output should project source A1 as inline text");
    check_contains(erase_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate rich shared string erase output should preserve source B1");
    check_contains(erase_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-rich-a2</t></is></c>)",
        "source max-coordinate rich shared string erase output should project source A2 as inline text");
    check(erase_entries.find("xl/sharedStrings.xml") != erase_entries.end() &&
            erase_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "source max-coordinate rich shared string erase output should preserve source sharedStrings bytes");
    check(erase_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "source max-coordinate rich shared string erase output should preserve untouched sheets byte-for-byte");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate rich shared string erase should not mutate the source package bytes");
}

void test_public_worksheet_editor_materializes_empty_source_worksheets()
{
    const auto write_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-empty-source")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-empty-source")});
        writer.close();
        return source;
    };

    const auto worksheet_xml = [](std::string_view body) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + std::string(body) + "</worksheet>";
    };

    const auto expect_empty_source_worksheet_materialization =
        [&](std::string_view tag, std::string_view replacement_worksheet_xml) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-empty-source-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-empty-source-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/worksheets/sheet1.xml") =
                std::string(replacement_worksheet_xml);
            write_stored_zip_entries(source, entries);

            fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
            fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
            check(sheet.cell_count() == 0,
                "empty source worksheet should materialize as an empty sparse store");
            check(!sheet.try_cell("A1").has_value(),
                "empty source worksheet should not invent an A1 sparse record");
            check(sheet.sparse_cells().empty(),
                "empty source worksheet should expose no sparse snapshots");
            check(!sheet.has_pending_changes(),
                "read-only empty source worksheet materialization should start clean");
            check(!editor.has_pending_changes(),
                "read-only empty source worksheet materialization should not dirty WorkbookEditor");

            const std::string inserted_text =
                std::string("empty-source-materialized-") + std::string(tag);
            sheet.set_cell("B2", fastxlsx::CellValue::text(inserted_text));
            editor.save_as(output);

            const auto output_entries = fastxlsx::test::read_zip_entries(output);
            const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
            check_contains(worksheet_xml, R"(<dimension ref="B2"/>)",
                "empty source worksheet edit should project a dimension from sparse records");
            check_contains(worksheet_xml,
                R"(<sheetData><row r="2"><c r="B2" t="inlineStr"><is><t>)"
                    + inserted_text + R"(</t></is></c></row></sheetData>)",
                "empty source worksheet edit should project standalone sheetData");
            check_not_contains(worksheet_xml, "placeholder-empty-source",
                "empty source worksheet materialization should not revive original placeholder cells");
            check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-empty-source",
                "empty source worksheet materialization should preserve untouched sheets");
        };

    expect_empty_source_worksheet_materialization(
        "missing-sheet-data", worksheet_xml(R"(<dimension ref="A1"/>)"));

    expect_empty_source_worksheet_materialization(
        "self-closing-sheet-data",
        worksheet_xml(R"(<dimension ref="A1"/><sheetData/>)"));
}

void test_public_worksheet_editor_drops_source_wrapper_metadata_on_dirty_projection()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-wrapper-metadata-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-wrapper-metadata-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-wrapper")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-wrapper-metadata")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetPr>ignored-wrapper-text<tabColor rgb="FFFF0000"/></sheetPr>)"
          R"(<dimension ref="A1"/>)"
          R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
          R"(<sheetFormatPr defaultRowHeight="15"/>)"
          R"(<cols><col min="1" max="1" width="20" customWidth="1"/></cols>)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-wrapper</t></is></c>)"
          R"(</row></sheetData>)"
          R"(<autoFilter ref="A1:A1"/>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "source-wrapper",
        "WorksheetEditor should materialize supported cells beside source wrapper metadata");
    check(!sheet.has_pending_changes(),
        "read-only source wrapper metadata materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source wrapper metadata materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source wrapper metadata materialization should not queue Patch edits");

    sheet.set_cell("B2", fastxlsx::CellValue::text("wrapper-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "dirty source wrapper metadata projection should generate sparse-store dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-wrapper</t></is></c>)",
        "dirty source wrapper metadata projection should keep materialized source cells");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>wrapper-new-inline</t></is></c>)",
        "dirty source wrapper metadata projection should write new inline text");
    check_not_contains(worksheet_xml, "<sheetPr",
        "dirty materialized projection should not preserve source sheetPr metadata");
    check_not_contains(worksheet_xml, "tabColor",
        "dirty materialized projection should not preserve source tabColor metadata");
    check_not_contains(worksheet_xml, "ignored-wrapper-text",
        "dirty materialized projection should not preserve source wrapper metadata text");
    check_not_contains(worksheet_xml, "<sheetViews",
        "dirty materialized projection should not preserve source sheetViews metadata");
    check_not_contains(worksheet_xml, "<sheetFormatPr",
        "dirty materialized projection should not preserve source sheetFormatPr metadata");
    check_not_contains(worksheet_xml, "<cols>",
        "dirty materialized projection should not preserve source cols metadata");
    check_not_contains(worksheet_xml, "<autoFilter",
        "dirty materialized projection should not preserve source autoFilter metadata");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-wrapper-metadata",
        "dirty source wrapper metadata projection should preserve untouched sheets");
}

void test_public_worksheet_editor_drops_relationship_wrapper_metadata_without_pruning()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-relationship-wrapper-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-relationship-wrapper-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("Name"),
            fastxlsx::CellView::text("Value")});
        data.append_row({fastxlsx::CellView::text("source-link-row"),
            fastxlsx::CellView::number(7.0)});
        data.add_external_hyperlink(2, 1, "https://example.com/source-wrapper-link");

        fastxlsx::TableOptions table;
        table.name = "RelationshipWrapperTable";
        table.column_names = {"Name", "Value"};
        data.add_table({1, 1, 2, 2}, table);

        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-relationship-wrapper")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_worksheet_xml =
        source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_worksheet_xml, "<hyperlinks>",
        "source relationship-wrapper fixture should contain hyperlinks metadata");
    check_contains(source_worksheet_xml, "<tableParts",
        "source relationship-wrapper fixture should contain tableParts metadata");
    check(source_entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "source relationship-wrapper fixture should contain worksheet relationships");
    check(source_entries.contains("xl/tables/table1.xml"),
        "source relationship-wrapper fixture should contain a table part");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    const std::optional<fastxlsx::CellValue> b2 = sheet.try_cell("B2");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Text
            && a2->text_value() == "source-link-row",
        "WorksheetEditor should materialize source cells beside relationship wrapper metadata");
    check(b2.has_value() && b2->kind() == fastxlsx::CellValueKind::Number
            && b2->number_value() == 7.0,
        "WorksheetEditor should materialize source numbers beside relationship wrapper metadata");
    check(!sheet.has_pending_changes(),
        "relationship wrapper metadata materialization should start clean");

    sheet.set_cell("C3", fastxlsx::CellValue::text("relationship-wrapper-new"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-link-row</t></is></c>)",
        "dirty relationship wrapper projection should keep materialized source text");
    check_contains(worksheet_xml, R"(<c r="B2"><v>7</v></c>)",
        "dirty relationship wrapper projection should keep materialized source number");
    check_contains(worksheet_xml,
        R"(<c r="C3" t="inlineStr"><is><t>relationship-wrapper-new</t></is></c>)",
        "dirty relationship wrapper projection should include the new edit");
    check_not_contains(worksheet_xml, "<hyperlinks>",
        "dirty relationship wrapper projection should drop source hyperlinks XML");
    check_not_contains(worksheet_xml, "<tableParts",
        "dirty relationship wrapper projection should drop source tableParts XML");
    check_not_contains(worksheet_xml, "r:id",
        "dirty relationship wrapper projection should not keep source relationship references");
    check(output_entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "dirty projection should not prune the source worksheet relationships part");
    check(output_entries.at("xl/worksheets/_rels/sheet1.xml.rels")
            == source_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "dirty projection should preserve source worksheet relationship bytes");
    check(output_entries.contains("xl/tables/table1.xml"),
        "dirty projection should not prune the source table part");
    check(output_entries.at("xl/tables/table1.xml")
            == source_entries.at("xl/tables/table1.xml"),
        "dirty projection should preserve source table bytes");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"),
        "keep-relationship-wrapper",
        "dirty relationship wrapper projection should preserve untouched sheets");
}

void test_public_worksheet_editor_drops_range_wrapper_metadata_on_dirty_projection()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-range-wrapper-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-range-wrapper-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-range-wrapper")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-range-wrapper")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:C3"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>range-wrapper-source</t></is></c>)"
          R"(<c r="B1"><v>3</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="b"><v>1</v></c>)"
          R"(</row>)"
          R"(</sheetData>)"
          R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"
          R"(<dataValidations count="1">)"
          R"(<dataValidation type="whole" operator="between" sqref="B2:B3">)"
          R"(<formula1>1</formula1><formula2>10</formula2>)"
          R"(</dataValidation>)"
          R"(</dataValidations>)"
          R"(<conditionalFormatting sqref="B2:B3">)"
          R"(<cfRule type="cellIs" priority="1" operator="greaterThan">)"
          R"(<formula>5</formula>)"
          R"(</cfRule>)"
          R"(</conditionalFormatting>)"
          R"(<ignoredErrors><ignoredError sqref="A1:C3" numberStoredAsText="1"/></ignoredErrors>)"
          R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
          R"(<pageSetup orientation="landscape"/>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "range-wrapper-source",
        "WorksheetEditor should materialize source text beside range wrapper metadata");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Number
            && b1->number_value() == 3.0,
        "WorksheetEditor should materialize source numbers beside range wrapper metadata");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Boolean
            && a2->boolean_value(),
        "WorksheetEditor should materialize source booleans beside range wrapper metadata");
    check(!sheet.has_pending_changes(),
        "range wrapper metadata materialization should start clean");
    check(!editor.has_pending_changes(),
        "range wrapper metadata materialization should not dirty WorkbookEditor");

    sheet.set_cell("C3", fastxlsx::CellValue::text("range-wrapper-new"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "dirty range wrapper projection should keep the sparse-store dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>range-wrapper-source</t></is></c>)",
        "dirty range wrapper projection should keep materialized source text");
    check_contains(worksheet_xml, R"(<c r="B1"><v>3</v></c>)",
        "dirty range wrapper projection should keep materialized source number");
    check_contains(worksheet_xml, R"(<c r="A2" t="b"><v>1</v></c>)",
        "dirty range wrapper projection should keep materialized source boolean");
    check_contains(worksheet_xml,
        R"(<c r="C3" t="inlineStr"><is><t>range-wrapper-new</t></is></c>)",
        "dirty range wrapper projection should include the new edit");
    check_not_contains(worksheet_xml, "<mergeCells",
        "dirty range wrapper projection should drop source mergeCells metadata");
    check_not_contains(worksheet_xml, "<dataValidations",
        "dirty range wrapper projection should drop source dataValidations metadata");
    check_not_contains(worksheet_xml, "<conditionalFormatting",
        "dirty range wrapper projection should drop source conditionalFormatting metadata");
    check_not_contains(worksheet_xml, "<ignoredErrors",
        "dirty range wrapper projection should drop source ignoredErrors metadata");
    check_not_contains(worksheet_xml, "<pageMargins",
        "dirty range wrapper projection should drop source pageMargins metadata");
    check_not_contains(worksheet_xml, "<pageSetup",
        "dirty range wrapper projection should drop source pageSetup metadata");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-range-wrapper",
        "dirty range wrapper projection should preserve untouched sheets");
}

void test_public_worksheet_editor_drops_source_comments_and_processing_instructions_on_dirty_projection()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-comments-pi-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-comments-pi-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-comments-pi")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-comments-pi")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<!--source-comment-before-root-->)"
          R"(<?source-pi-before-root keep?>)"
          R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<!--source-comment-inside-root-->)"
          R"(<?source-pi-inside-root keep?>)"
          R"(<sheetData>)"
          R"(<!--source-comment-inside-sheetData-->)"
          R"(<?source-pi-inside-sheetData keep?>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-comments-pi</t></is></c>)"
          R"(</row>)"
          R"(<!--source-comment-after-row-->)"
          R"(</sheetData>)"
          R"(<?source-pi-after-sheetData keep?>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "source-comments-pi",
        "WorksheetEditor should materialize supported cells beside source comments and processing instructions");
    check(!sheet.has_pending_changes(),
        "read-only source comment/PI materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source comment/PI materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source comment/PI materialization should not queue Patch edits");

    sheet.set_cell("B2", fastxlsx::CellValue::text("comments-pi-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "dirty source comment/PI projection should generate sparse-store dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-comments-pi</t></is></c>)",
        "dirty source comment/PI projection should keep materialized source cells");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>comments-pi-new-inline</t></is></c>)",
        "dirty source comment/PI projection should write new inline text");
    check_not_contains(worksheet_xml, "source-comment-",
        "dirty materialized projection should not preserve source comments");
    check_not_contains(worksheet_xml, "source-pi-",
        "dirty materialized projection should not preserve source processing instructions");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-comments-pi",
        "dirty source comment/PI projection should preserve untouched sheets");
}

void test_public_worksheet_editor_read_only_materialization_keeps_noop_save_as_copy_original()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-readonly-materialized-noop-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-readonly-materialized-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("noop-shared-a"),
            fastxlsx::CellView::text("noop-shared-b")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-noop-materialized")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<!--readonly-noop-comment-before-root-->)"
          R"(<?readonly-noop-pi keep?>)"
          R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetPr><tabColor rgb="FF00FF00"/></sheetPr>)"
          R"(<dimension ref="A1:B1"/>)"
          R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1" t="s"><v>0</v></c>)"
          R"(<c r="B1" t="s"><v>1</v></c>)"
          R"(</row></sheetData>)"
          R"(<autoFilter ref="A1:B1"/>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "noop-shared-a",
        "read-only no-op materialization should still read source shared string A1");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "noop-shared-b",
        "read-only no-op materialization should still read source shared string B1");
    check(!sheet.has_pending_changes(),
        "read-only no-op materialization should keep the sheet clean");
    check(!editor.has_pending_changes(),
        "read-only no-op materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "read-only no-op materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only no-op materialization should not expose dirty materialized names");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "no-op save_as after read-only materialization should keep the sheet clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after read-only materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after read-only materialization should not create public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "no-op save_as after read-only materialization should not expose dirty names");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as after read-only materialization should copy source entries");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "readonly-noop-comment-before-root",
        "no-op save_as after read-only materialization should preserve source comments");
    check_contains(worksheet_xml, "readonly-noop-pi",
        "no-op save_as after read-only materialization should preserve source processing instructions");
    check_contains(worksheet_xml, "<sheetPr>",
        "no-op save_as after read-only materialization should preserve source wrapper metadata");
    check_contains(worksheet_xml, R"(<c r="A1" t="s"><v>0</v></c>)",
        "no-op save_as after read-only materialization should preserve source shared string indexes");
    check_not_contains(worksheet_xml, R"(t="inlineStr")",
        "no-op save_as after read-only materialization should not flush inline-string projection");
}

void test_public_worksheet_editor_materializes_source_shared_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-source.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("shared-a"),
            fastxlsx::CellView::text("A&B <C>")});
        data.append_row({fastxlsx::CellView::text("shared-a")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-shared")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "shared-string source should emit a sharedStrings part for materialization");
    std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    replace_first_or_throw(shared_strings_before, "?><sst",
        "?><?fastxlsx sharedStrings-trivia?>"
        "<?fastxlsx.data-1:probe legal-target?>"
        "<?_fastxlsx legal-start?>"
        "<?:fastxlsx legal-colon-start?>"
        "<?fastxlsx?>"
        "<?xml-stylesheet type=\"text/xsl\" href=\"sharedStrings.xsl\"?><sst");
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings_before);
    {
        std::string updated_workbook_rels = source_entries.at("xl/_rels/workbook.xml.rels");
        replace_first_or_throw(updated_workbook_rels,
            R"(Target="sharedStrings.xml")",
            R"(Target="./sharedStrings.xml")");
        rewrite_package_entry_as_stored(
            source, "xl/_rels/workbook.xml.rels", updated_workbook_rels);
    }

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "shared-a",
        "WorksheetEditor should materialize A1 shared string text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "A&B <C>",
        "WorksheetEditor should decode XML entities from source sharedStrings");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Text
            && a2->text_value() == "shared-a",
        "WorksheetEditor should materialize repeated shared string indexes");
    check(shared_strings_before.find("<?fastxlsx sharedStrings-trivia?>")
            != std::string::npos,
        "source sharedStrings success fixture should include prolog processing instruction trivia");
    check(shared_strings_before.find("<?fastxlsx.data-1:probe legal-target?>")
            != std::string::npos,
        "source sharedStrings success fixture should include legal PI target continuation trivia");
    check(shared_strings_before.find("<?_fastxlsx legal-start?>") != std::string::npos,
        "source sharedStrings success fixture should include underscore-start PI target trivia");
    check(shared_strings_before.find("<?:fastxlsx legal-colon-start?>") != std::string::npos,
        "source sharedStrings success fixture should include colon-start PI target trivia");
    check(shared_strings_before.find("<?fastxlsx?>") != std::string::npos,
        "source sharedStrings success fixture should include empty-data PI trivia");
    check(shared_strings_before.find("<?xml-stylesheet") != std::string::npos,
        "source sharedStrings success fixture should include xml-stylesheet PI trivia");
    check(shared_strings_before.find(R"(standalone="yes")") != std::string::npos,
        "source sharedStrings success fixture should include legal standalone declaration metadata");
    check(!sheet.has_pending_changes(),
        "read-only source sharedStrings materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source sharedStrings materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source sharedStrings materialization should not queue Patch edits");

    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-output.xlsx");
    sheet.set_cell("C3", fastxlsx::CellValue::text("new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>shared-a</t></is></c>)",
        "flushed WorksheetEditor source shared string should be projected as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>A&amp;B &lt;C&gt;</t></is></c>)",
        "flushed WorksheetEditor source shared string should be XML escaped inline text");
    check_contains(worksheet_xml,
        R"(<c r="C3" t="inlineStr"><is><t>new-inline</t></is></c>)",
        "new WorksheetEditor text should continue to write inline strings");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "WorksheetEditor save_as should preserve source sharedStrings bytes, not rebuild them");
}

void test_public_worksheet_editor_accepts_legal_source_shared_strings_xml_declarations()
{
    struct LegalDeclarationCase {
        std::string_view name;
        std::string_view declaration;
        std::string_view expected_text;
    };

    const std::array<LegalDeclarationCase, 2> cases{{
        {"single-quoted-version-1-1-with-encoding-and-standalone-no",
            "<?xml version='1.1' encoding='UTF_8-Test.1' standalone='no'?>",
            "legal-declaration-version-1-1"},
        {"version-only-single-quoted",
            "<?xml version='1.0'?>",
            "legal-declaration-version-only"},
    }};

    for (const LegalDeclarationCase& test_case : cases) {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-output.xlsx");
        {
            fastxlsx::WorkbookWriterOptions options;
            options.string_strategy = fastxlsx::StringStrategy::SharedString;
            fastxlsx::WorkbookWriter writer =
                fastxlsx::WorkbookWriter::create(source, options);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("declaration-placeholder")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-legal-declaration")});
            writer.close();
        }

        const std::string shared_strings_xml =
            std::string(test_case.declaration)
            + R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
              R"(<si><t>)"
            + std::string(test_case.expected_text)
            + R"(</t></si><si><t>keep-legal-declaration</t></si></sst>)";
        rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings_xml);
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
        check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
                && a1->text_value() == test_case.expected_text,
            std::string(test_case.name)
                + " should materialize source sharedStrings text");
        check(!sheet.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration materialization should start clean");
        check(!editor.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration materialization should not dirty WorkbookEditor");
        check(editor.pending_change_count() == 0,
            std::string(test_case.name)
                + " legal declaration materialization should not queue Patch edits");

        sheet.set_cell("B2", fastxlsx::CellValue::text("legal-declaration-new-inline"));
        editor.save_as(output);

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml,
            std::string(R"(<c r="A1" t="inlineStr"><is><t>)")
                + std::string(test_case.expected_text) + R"(</t></is></c>)",
            std::string(test_case.name)
                + " dirty projection should write materialized text inline");
        check_contains(worksheet_xml,
            R"(<c r="B2" t="inlineStr"><is><t>legal-declaration-new-inline</t></is></c>)",
            std::string(test_case.name)
                + " dirty projection should include edits beside legal declaration source text");
        check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
                && output_entries.at("xl/sharedStrings.xml") == shared_strings_xml,
            std::string(test_case.name)
                + " dirty projection should preserve legal declaration sharedStrings bytes");
        check(output_entries.at("xl/worksheets/sheet2.xml")
                == source_entries.at("xl/worksheets/sheet2.xml"),
            std::string(test_case.name)
                + " dirty projection should preserve untouched sheet bytes");
    }
}

void test_public_worksheet_editor_flattens_rich_source_shared_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-rich-sharedstrings-source.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("rich-placeholder"),
            fastxlsx::CellView::text("plain-placeholder")});
        writer.close();
    }

    const std::string rich_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><r><t>rich-</t></r><r><t>A&amp;B</t></r><rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh><phoneticPr fontId="1"/></si>)"
        R"(<si><t>plain</t></si>)"
        R"(</sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", rich_shared_strings);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "rich-A&B",
        "WorksheetEditor should flatten simple source sharedStrings rich text runs");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "plain",
        "WorksheetEditor should still materialize plain shared string items beside rich text");
    check(!sheet.has_pending_changes(),
        "rich sharedStrings read-only materialization should start clean");
}

void test_public_worksheet_editor_materializes_prefixed_source_shared_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-dirty-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("prefix-placeholder-a"),
            fastxlsx::CellView::text("prefix-placeholder-b")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-prefixed-shared")});
        writer.close();
    }

    const std::string prefixed_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<x:sst xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test" count="2" uniqueCount="2">)"
        R"(<x:si><x:t>prefixed-A&amp;B</x:t></x:si>)"
        R"(<x:si><x:r><x:rPr><x:b/></x:rPr><x:t>rich-</x:t></x:r><x:r><x:t xml:space="preserve"> tail </x:t></x:r>)"
        R"(<x:rPh sb="1" eb="1"/><x:phoneticPr fontId="1"/><x:extLst/>)"
        R"(<x:rPh sb="0" eb="1"><fx:opaque><x:r><x:t>ignored-nested-phonetic</x:t></x:r></fx:opaque></x:rPh>)"
        R"(<x:extLst><x:ext uri="{fastxlsx-test}"><fx:opaque><x:r><x:t>ignored-nested-ext</x:t></x:r></fx:opaque></x:ext></x:extLst></x:si>)"
        R"(<x:phoneticPr fontId="2"/><x:extLst/><x:extLst><x:ext uri="{fastxlsx-root}"><fx:opaque><x:t>ignored-root-ext</x:t></fx:opaque></x:ext></x:extLst>)"
        R"(</x:sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", prefixed_shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, "<x:sst",
        "prefixed sharedStrings fixture should use a qualified root element");
    check_contains(shared_strings_before, "<x:si>",
        "prefixed sharedStrings fixture should use qualified shared string items");
    check_contains(shared_strings_before, "<x:t>",
        "prefixed sharedStrings fixture should use qualified text elements");
    check_contains(shared_strings_before, "ignored-nested-ext",
        "prefixed sharedStrings fixture should carry nested ignored extension text");
    check_contains(shared_strings_before, "ignored-root-ext",
        "prefixed sharedStrings fixture should carry root-level ignored extension text");
    check_contains(shared_strings_before, "<x:extLst/>",
        "prefixed sharedStrings fixture should carry self-closing ignored metadata");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "prefixed-A&B",
        "WorksheetEditor should materialize prefixed source sharedStrings text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "rich- tail ",
        "WorksheetEditor should flatten prefixed rich sharedStrings by local-name");
    check(!sheet.has_pending_changes(),
        "prefixed sharedStrings read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "prefixed sharedStrings read-only materialization should not dirty WorkbookEditor");

    editor.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after prefixed sharedStrings materialization should copy source entries");

    sheet.set_cell("C1", fastxlsx::CellValue::text("prefixed-shared-dirty"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>prefixed-A&amp;B</t></is></c>)",
        "dirty projection should write prefixed source sharedStrings text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t xml:space="preserve">rich- tail </t></is></c>)",
        "dirty projection should preserve flattened prefixed rich sharedStrings whitespace");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>prefixed-shared-dirty</t></is></c>)",
        "dirty projection should include edits beside prefixed source sharedStrings");
    check_not_contains(worksheet_xml, "ignored-nested-phonetic",
        "dirty projection should not leak nested ignored sharedStrings phonetic text");
    check_not_contains(worksheet_xml, "ignored-nested-ext",
        "dirty projection should not leak nested ignored sharedStrings extension text");
    check_not_contains(worksheet_xml, "ignored-root-ext",
        "dirty projection should not leak root-level ignored sharedStrings extension text");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty projection should preserve prefixed source sharedStrings bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty prefixed sharedStrings projection should preserve untouched sheets");
}

void test_public_worksheet_editor_materializes_local_names_without_namespace_validation()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-noop.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-dirty.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("wrong-ns-placeholder-a"),
            fastxlsx::CellView::text("wrong-ns-placeholder-b")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-wrong-namespace")});
        writer.close();
    }

    const std::string wrong_namespace_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<bad:sst xmlns:bad="urn:fastxlsx:not-spreadsheetml" count="2" uniqueCount="2">)"
        R"(<bad:si><bad:t>wrong-ns-shared</bad:t></bad:si>)"
        R"(<bad:si><bad:r><bad:t>wrong-rich-</bad:t></bad:r><bad:r><bad:t>tail</bad:t></bad:r></bad:si>)"
        R"(</bad:sst>)";
    rewrite_package_entry_as_stored(
        source, "xl/sharedStrings.xml", wrong_namespace_shared_strings);

    const std::string wrong_namespace_worksheet =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<bad:worksheet xmlns:bad="urn:fastxlsx:not-spreadsheetml">)"
          R"(<bad:sheetData><bad:row r="1">)"
          R"(<bad:c r="A1" t="s"><bad:v>0</bad:v></bad:c>)"
          R"(<bad:c r="B1" t="inlineStr"><bad:is><bad:t>wrong-ns-inline</bad:t></bad:is></bad:c>)"
          R"(<bad:c r="C1" t="s"><bad:v>1</bad:v></bad:c>)"
          R"(</bad:row></bad:sheetData>)"
          R"(</bad:worksheet>)";
    rewrite_package_entry_as_stored(
        source, "xl/worksheets/sheet1.xml", wrong_namespace_worksheet);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/sharedStrings.xml"), "urn:fastxlsx:not-spreadsheetml",
        "wrong-namespace local-name fixture should use a non-spreadsheetml sharedStrings URI");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "urn:fastxlsx:not-spreadsheetml",
        "wrong-namespace local-name fixture should use a non-spreadsheetml worksheet URI");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "wrong-ns-shared",
        "WorksheetEditor should materialize sharedStrings by local-name without namespace URI validation");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "wrong-ns-inline",
        "WorksheetEditor should materialize inline strings by local-name without namespace URI validation");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Text
            && c1->text_value() == "wrong-rich-tail",
        "WorksheetEditor should flatten rich sharedStrings by local-name without namespace URI validation");
    check(!sheet.has_pending_changes(),
        "wrong-namespace local-name materialization should start clean");
    check(!editor.has_pending_changes(),
        "wrong-namespace local-name materialization should not dirty WorkbookEditor");

    editor.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after wrong-namespace local-name materialization should copy source entries");

    sheet.set_cell("D1", fastxlsx::CellValue::text("wrong-ns-dirty"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>wrong-ns-shared</t></is></c>)",
        "dirty projection should write wrong-namespace sharedStrings text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>wrong-ns-inline</t></is></c>)",
        "dirty projection should write wrong-namespace inline text as plain inline text");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>wrong-rich-tail</t></is></c>)",
        "dirty projection should write flattened wrong-namespace sharedStrings rich text");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t>wrong-ns-dirty</t></is></c>)",
        "dirty projection should include edits beside wrong-namespace local-name source cells");
    check_not_contains(worksheet_xml, "urn:fastxlsx:not-spreadsheetml",
        "dirty standalone projection should not preserve wrong source namespace declarations");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml")
                == source_entries.at("xl/sharedStrings.xml"),
        "dirty wrong-namespace projection should preserve source sharedStrings bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty wrong-namespace projection should preserve untouched sheets");
}

void test_public_worksheet_editor_materializes_source_shared_strings_xml_space_and_projects_inline()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-dirty-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("space-placeholder"),
            fastxlsx::CellView::text("rich-space-placeholder")});
        writer.close();
    }

    const std::string shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><t xml:space="preserve">  plain &amp; space  </t></si>)"
        R"(<si><r><t xml:space="preserve">  rich </t></r><r><t>&amp; B</t></r><r><t xml:space="preserve"> tail  </t></r></si>)"
        R"(</sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "  plain & space  ",
        "WorksheetEditor should preserve xml:space whitespace from plain sharedStrings text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "  rich & B tail  ",
        "WorksheetEditor should preserve xml:space whitespace while flattening rich sharedStrings runs");
    check(!sheet.has_pending_changes(),
        "source sharedStrings xml:space materialization should start clean");

    editor.save_as(noop_output);
    check(!editor.has_pending_changes(),
        "no-op save_as after sharedStrings xml:space materialization should keep editor clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after sharedStrings xml:space materialization should copy source entries");

    sheet.set_cell("C1", fastxlsx::CellValue::text("dirty-space-trigger"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t xml:space="preserve">  plain &amp; space  </t></is></c>)",
        "dirty projection should write source sharedStrings whitespace as inline text with xml:space");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t xml:space="preserve">  rich &amp; B tail  </t></is></c>)",
        "dirty projection should flatten rich sharedStrings whitespace into inline text with xml:space");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>dirty-space-trigger</t></is></c>)",
        "dirty projection should include the new trigger edit");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty projection should preserve source sharedStrings bytes with xml:space markup");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings xml:space projection should not mutate the source package");
}

void test_public_worksheet_editor_ignores_source_shared_strings_counts_and_unknown_attributes()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-dirty-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("first-placeholder"),
            fastxlsx::CellView::text("second-placeholder")});
        writer.close();
    }

    const std::string shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test" count="999" uniqueCount="0" fx:root="ignored">)"
        R"(<si fx:item="first"><t fx:text="first">first-meta</t></si>)"
        R"(<si fx:item="second"><r fx:run="1"><t fx:text="second">second</t></r><r fx:run="2"><t>-meta</t></r></si>)"
        R"(</sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, R"(count="999")",
        "source sharedStrings metadata fixture should carry inconsistent count");
    check_contains(shared_strings_before, R"(uniqueCount="0")",
        "source sharedStrings metadata fixture should carry inconsistent uniqueCount");
    check_contains(shared_strings_before, R"(fx:root="ignored")",
        "source sharedStrings metadata fixture should carry unknown root attributes");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "first-meta",
        "WorksheetEditor should use actual sharedStrings item text, not root count metadata");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "second-meta",
        "WorksheetEditor should ignore unknown sharedStrings item/run/text attributes");
    check(!sheet.has_pending_changes(),
        "source sharedStrings count/attribute materialization should start clean");
    check(!editor.has_pending_changes(),
        "source sharedStrings count/attribute materialization should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "source sharedStrings count/attribute materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!editor.has_pending_changes(),
        "no-op save_as after sharedStrings count/attribute materialization should keep editor clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after sharedStrings count/attribute materialization should copy source entries");

    sheet.set_cell("C1", fastxlsx::CellValue::text("after-metadata"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>first-meta</t></is></c>)",
        "dirty projection should write count-mismatched source sharedStrings as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>second-meta</t></is></c>)",
        "dirty projection should write unknown-attribute source sharedStrings as flattened inline text");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>after-metadata</t></is></c>)",
        "dirty projection should include the metadata-boundary trigger edit");
    check_not_contains(worksheet_xml, R"(t="s")",
        "dirty projection should not write shared string indexes after materialization");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty projection should preserve source sharedStrings bytes with inconsistent metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings count/attribute projection should not mutate the source package");
}

void test_public_worksheet_editor_materializes_source_formulas()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-formula-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(2.0),
            fastxlsx::CellView::number(3.0),
            fastxlsx::CellView::formula("SUM(A1:B1)&\"<ok>\"")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    std::string& source_worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    replace_first_or_throw(source_worksheet_xml,
        R"(<c r="C1"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f></c>)",
        R"(<c r="C1"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f><v>999</v></c>)");
    write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 2.0,
        "WorksheetEditor should materialize source formula sibling number A1");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Number
            && b1->number_value() == 3.0,
        "WorksheetEditor should materialize source formula sibling number B1");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Formula
            && c1->text_value() == "SUM(A1:B1)&\"<ok>\"",
        "WorksheetEditor should materialize source formula text and ignore cached values");
    check(!sheet.has_pending_changes(),
        "source formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "source formula read-only materialization should not dirty the workbook editor");

    sheet.set_cell("D2", fastxlsx::CellValue::text("formula-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="C1"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f></c>)",
        "flushed WorksheetEditor source formula should preserve formula text");
    check(worksheet_xml.find("<v>999</v>") == std::string::npos,
        "flushed WorksheetEditor source formula should not preserve stale cached values");
    check_contains(worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>formula-new-inline</t></is></c>)",
        "flushed WorksheetEditor source formula sheet should include later text edits");
}

void test_public_worksheet_editor_materializes_source_error_cells()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-source-error-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-error-cells-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1" t="e"><v>#VALUE!</v></c>)"
        R"(<c r="B1" t="e"><v>#DIV/0!</v></c>)"
        R"(<c r="C1" t="e"><v>#N/A</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Error
            && a1->text_value() == "#VALUE!",
        "WorksheetEditor should materialize source #VALUE! error cells");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Error
            && b1->text_value() == "#DIV/0!",
        "WorksheetEditor should materialize source #DIV/0! error cells");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Error
            && c1->text_value() == "#N/A",
        "WorksheetEditor should materialize source #N/A error cells");
    check(!sheet.has_pending_changes(),
        "source error cell read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "source error cell read-only materialization should not dirty the workbook editor");

    sheet.set_cell("D2", fastxlsx::CellValue::text("after-source-error"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml, R"(<c r="A1" t="e"><v>#VALUE!</v></c>)",
        "dirty projection should write materialized #VALUE! as t=e");
    check_contains(output_worksheet_xml, R"(<c r="B1" t="e"><v>#DIV/0!</v></c>)",
        "dirty projection should write materialized #DIV/0! as t=e");
    check_contains(output_worksheet_xml, R"(<c r="C1" t="e"><v>#N/A</v></c>)",
        "dirty projection should write materialized #N/A as t=e");
    check_contains(output_worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>after-source-error</t></is></c>)",
        "dirty projection should include edits beside source error cells");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty source error projection should preserve untouched sheets");
}

void test_public_worksheet_editor_ignores_formula_cached_result_types()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-formula-cached-result-types-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-formula-cached-result-types-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1"><f>A2+1</f><v>999</v></c>)"
        R"(<c r="B1" t="str"><f>TEXT(A1,"@")</f><v>stale-string</v></c>)"
        R"(<c r="C1" t="b"><f>A1&gt;0</f><v>1</v></c>)"
        R"(<c r="D1" t="e"><f>NA()</f><v>#N/A</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> d1 = sheet.try_cell("D1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Formula
            && a1->text_value() == "A2+1",
        "WorksheetEditor should ignore numeric cached results when source formula text exists");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Formula
            && b1->text_value() == "TEXT(A1,\"@\")",
        "WorksheetEditor should ignore t=str cached results when source formula text exists");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Formula
            && c1->text_value() == "A1>0",
        "WorksheetEditor should ignore boolean cached results when source formula text exists");
    check(d1.has_value() && d1->kind() == fastxlsx::CellValueKind::Formula
            && d1->text_value() == "NA()",
        "WorksheetEditor should ignore error cached results when source formula text exists");
    check(!sheet.has_pending_changes(),
        "formula cached-result materialization should start clean");
    check(!editor.has_pending_changes(),
        "formula cached-result materialization should not dirty the workbook editor");

    sheet.set_cell("D2", fastxlsx::CellValue::text("cached-result-types-edit"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml, R"(<c r="A1"><f>A2+1</f></c>)",
        "dirty projection should write numeric-cached formulas without stale values");
    check_contains(output_worksheet_xml, R"(<c r="B1"><f>TEXT(A1,"@")</f></c>)",
        "dirty projection should write t=str-cached formulas without stale values");
    check_contains(output_worksheet_xml, R"(<c r="C1"><f>A1&gt;0</f></c>)",
        "dirty projection should write boolean-cached formulas without stale values");
    check_contains(output_worksheet_xml, R"(<c r="D1"><f>NA()</f></c>)",
        "dirty projection should write error-cached formulas without stale values");
    check_not_contains(output_worksheet_xml, "<v>999</v>",
        "dirty projection should drop stale numeric cached formula values");
    check_not_contains(output_worksheet_xml, "stale-string",
        "dirty projection should drop stale string cached formula values");
    check_not_contains(output_worksheet_xml, "<v>1</v>",
        "dirty projection should drop stale boolean cached formula values");
    check_not_contains(output_worksheet_xml, "<v>#N/A</v>",
        "dirty projection should drop stale error cached formula values");
    check_contains(output_worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>cached-result-types-edit</t></is></c>)",
        "dirty projection should include later edits beside cached-result formulas");
    check(fastxlsx::test::read_zip_entries(source).at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "formula cached-result rewrite should not mutate untouched source sheet bytes");
}

void test_public_worksheet_editor_materializes_source_shared_formulas()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-source-shared-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-shared-formula-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData>)"
        R"(<row r="1">)"
        R"(<c r="A1"><f t="shared" ref="A1:B2" si="5" ca="1">A1+B$1+$A1+$A$1+SUM(A1:B1)&amp;"A1"+'Other Sheet'!A1+[Book.xlsx]Sheet1!A1+Table1[A1]</f><v>123</v></c>)"
        R"(</row>)"
        R"(<row r="2">)"
        R"(<c r="B2"><f t="shared" si="5" aca="1" bx="1"/><v>999</v></c>)"
        R"(</row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> base = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> follower = sheet.try_cell("B2");
    check(base.has_value() && base->kind() == fastxlsx::CellValueKind::Formula
            && base->text_value()
                == R"(A1+B$1+$A1+$A$1+SUM(A1:B1)&"A1"+'Other Sheet'!A1+[Book.xlsx]Sheet1!A1+Table1[A1])",
        "WorksheetEditor should materialize shared formula definitions as formula text");
    check(follower.has_value() && follower->kind() == fastxlsx::CellValueKind::Formula
            && follower->text_value()
                == R"(B2+C$1+$A2+$A$1+SUM(B2:C2)&"A1"+'Other Sheet'!B2+[Book.xlsx]Sheet1!B2+Table1[A1])",
        "WorksheetEditor should materialize shared formula followers with translated references");
    check(!sheet.has_pending_changes(),
        "source shared formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "source shared formula read-only materialization should not dirty the workbook editor");

    sheet.set_cell("C3", fastxlsx::CellValue::text("shared-formula-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml,
        R"(<c r="A1"><f>A1+B$1+$A1+$A$1+SUM(A1:B1)&amp;"A1"+'Other Sheet'!A1+[Book.xlsx]Sheet1!A1+Table1[A1]</f></c>)",
        "flushed WorksheetEditor shared formula base should write plain formula text");
    check_contains(output_worksheet_xml,
        R"(<c r="B2"><f>B2+C$1+$A2+$A$1+SUM(B2:C2)&amp;"A1"+'Other Sheet'!B2+[Book.xlsx]Sheet1!B2+Table1[A1]</f></c>)",
        "flushed WorksheetEditor shared formula follower should write translated formula text");
    check_not_contains(output_worksheet_xml, "<v>999</v>",
        "flushed WorksheetEditor shared formula follower should not preserve stale cached values");
    check_not_contains(output_worksheet_xml, R"(ca="1")",
        "flushed WorksheetEditor shared formulas should not preserve calc metadata attributes");
    check_not_contains(output_worksheet_xml, R"(aca="1")",
        "flushed WorksheetEditor shared formula followers should not preserve metadata attributes");
    check_contains(output_worksheet_xml,
        R"(<c r="C3" t="inlineStr"><is><t>shared-formula-new-inline</t></is></c>)",
        "flushed WorksheetEditor shared formula sheet should include later text edits");
    check(fastxlsx::test::read_zip_entries(source).at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "source shared formula rewrite should not mutate untouched source sheet bytes");
}

void test_public_worksheet_editor_materializes_source_order_shared_formula_matrix()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-source-shared-formula-matrix-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-shared-formula-matrix-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData>)"
        R"(<row r="1">)"
        R"(<c r="A1"><f t="shared" ref="A1:C2" si="1">A1+Sheet1!A1+'O''Brien'!A1+SUM(A1:B1)+LOG10(A1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(1:1)+SUM(Sheet1!A:A)+SUM('Other Sheet'!1:1)+SUM($A:B)+SUM($1:2)</f><v>1</v></c>)"
        R"(<c r="B1"><f t="shared" ref="B1:D1" si="2">C1+D$1+$C1+$C$1</f><v>2</v></c>)"
        R"(<c r="C1"><f t="shared" si="1"/><v>777</v></c>)"
        R"(<c r="D1"><f t="shared" si="2"/><v>888</v></c>)"
        R"(</row>)"
        R"(<row r="2"><c r="A2"><f t="shared" si="1"/><v>999</v></c></row>)"
        R"(<row r="3"><c r="A3"><f t="shared" ref="A3:B3" si="1">Z3+1</f><v>3</v></c><c r="B3"><f t="shared" si="1"/><v>4</v></c></row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const auto expect_formula = [&](std::string_view reference, std::string_view expected,
                                    std::string_view message) {
        const std::optional<fastxlsx::CellValue> value = sheet.try_cell(reference);
        check(value.has_value() && value->kind() == fastxlsx::CellValueKind::Formula
                && value->text_value() == expected,
            message);
    };
    expect_formula("C1",
        "C1+Sheet1!C1+'O''Brien'!C1+SUM(C1:D1)+LOG10(C1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(C:C)+SUM(1:1)+SUM(Sheet1!C:C)+SUM('Other Sheet'!1:1)+SUM($A:D)+SUM($1:2)",
        "WorksheetEditor should translate multiple shared formula followers without formula-name false positives");
    expect_formula("D1", "E1+F$1+$C1+$C$1",
        "WorksheetEditor should keep interleaved shared formula indexes independent");
    expect_formula("A2",
        "A2+Sheet1!A2+'O''Brien'!A2+SUM(A2:B2)+LOG10(A2)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(2:2)+SUM(Sheet1!A:A)+SUM('Other Sheet'!2:2)+SUM($A:B)+SUM($1:3)",
        "WorksheetEditor should translate row-offset shared formula followers");
    expect_formula("B3", "AA3+1",
        "WorksheetEditor should use the latest source-order shared formula definition for later followers");
    check(!sheet.has_pending_changes(),
        "source-order shared formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "source-order shared formula read-only materialization should not dirty the workbook editor");

    sheet.set_cell("E4", fastxlsx::CellValue::text("shared-formula-matrix-edit"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml,
        R"(<c r="C1"><f>C1+Sheet1!C1+'O''Brien'!C1+SUM(C1:D1)+LOG10(C1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(C:C)+SUM(1:1)+SUM(Sheet1!C:C)+SUM('Other Sheet'!1:1)+SUM($A:D)+SUM($1:2)</f></c>)",
        "flushed WorksheetEditor should write translated source-order shared formula C1 as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="D1"><f>E1+F$1+$C1+$C$1</f></c>)",
        "flushed WorksheetEditor should write interleaved shared formula D1 as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="B3"><f>AA3+1</f></c>)",
        "flushed WorksheetEditor should write latest-definition shared formula B3 as plain formula text");
    check_not_contains(output_worksheet_xml, R"(t="shared")",
        "flushed WorksheetEditor should not preserve shared formula metadata");
    check_not_contains(output_worksheet_xml, "<v>999</v>",
        "flushed WorksheetEditor should drop stale shared formula cached values");
    check_contains(output_worksheet_xml,
        R"(<c r="E4" t="inlineStr"><is><t>shared-formula-matrix-edit</t></is></c>)",
        "flushed WorksheetEditor shared formula matrix should include later text edits");
    check(fastxlsx::test::read_zip_entries(source).at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "source-order shared formula matrix should not mutate untouched source sheet bytes");
}

void test_public_worksheet_editor_materializes_office_like_shared_formula_shape()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-office-like-shared-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-office-like-shared-formula-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<dimension ref="A1:H6"/>)"
        R"(<sheetData>)"
        R"(<row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c>)"
        R"(<c r="C1"><f t="shared" ref="C1:D3" si="40">A1+B1</f><v>9901</v></c>)"
        R"(<c r="D1"><f t="shared" si="40"/><v>9902</v></c>)"
        R"(<c r="E1"><f>A1*2</f><v>9903</v></c></row>)"
        R"(<row r="2"><c r="A2"><v>10</v></c><c r="B2"><v>20</v></c>)"
        R"(<c r="C2"><f t="shared" si="40"/><v>9904</v></c>)"
        R"(<c r="D2"><f t="shared" si="40"/><v>9905</v></c>)"
        R"(<c r="E2" t="inlineStr"><is><t>between-shared-groups</t></is></c>)"
        R"(<c r="F2"><f t="shared" ref="F2:G3" si="41">SUM($A2:B2)+C$1</f><v>9906</v></c>)"
        R"(<c r="G2"><f t="shared" si="41"/><v>9907</v></c></row>)"
        R"(<row r="3"><c r="A3"><v>100</v></c><c r="B3"><v>200</v></c>)"
        R"(<c r="C3"><f t="shared" si="40"/><v>9908</v></c>)"
        R"(<c r="D3"><f t="shared" si="40"/><v>9909</v></c>)"
        R"(<c r="F3"><f t="shared" si="41"/><v>9910</v></c>)"
        R"(<c r="G3"><f t="shared" si="41"/><v>9911</v></c></row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const auto expect_formula = [&](std::string_view reference, std::string_view expected,
                                    std::string_view message) {
        const std::optional<fastxlsx::CellValue> value = sheet.try_cell(reference);
        check(value.has_value() && value->kind() == fastxlsx::CellValueKind::Formula
                && value->text_value() == expected,
            message);
    };
    expect_formula("C1", "A1+B1",
        "WorksheetEditor should materialize the first 2D shared formula definition");
    expect_formula("D1", "B1+C1",
        "WorksheetEditor should translate a same-row 2D shared formula follower");
    expect_formula("C2", "A2+B2",
        "WorksheetEditor should translate a same-column 2D shared formula follower");
    expect_formula("D3", "B3+C3",
        "WorksheetEditor should translate a diagonal 2D shared formula follower");
    expect_formula("E1", "A1*2",
        "WorksheetEditor should preserve ordinary formulas interleaved with shared formulas");
    expect_formula("F2", "SUM($A2:B2)+C$1",
        "WorksheetEditor should materialize the second shared formula definition");
    expect_formula("G2", "SUM($A2:C2)+D$1",
        "WorksheetEditor should translate column-offset followers in the second shared formula group");
    expect_formula("G3", "SUM($A3:C3)+D$1",
        "WorksheetEditor should translate row and column offsets in the second shared formula group");
    check(!sheet.has_pending_changes(),
        "office-like shared formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "office-like shared formula read-only materialization should not dirty the workbook editor");

    sheet.set_cell("H6", fastxlsx::CellValue::text("office-like-shared-formula-edit"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml, R"(<c r="C1"><f>A1+B1</f></c>)",
        "flushed WorksheetEditor should write 2D shared formula definition as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="D1"><f>B1+C1</f></c>)",
        "flushed WorksheetEditor should write same-row 2D follower as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="D3"><f>B3+C3</f></c>)",
        "flushed WorksheetEditor should write diagonal 2D follower as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="G3"><f>SUM($A3:C3)+D$1</f></c>)",
        "flushed WorksheetEditor should write the second shared formula group follower");
    check_contains(output_worksheet_xml,
        R"(<c r="H6" t="inlineStr"><is><t>office-like-shared-formula-edit</t></is></c>)",
        "flushed WorksheetEditor office-like shared formula sheet should include later text edits");
    check_not_contains(output_worksheet_xml, R"(t="shared")",
        "flushed WorksheetEditor office-like shared formula sheet should not preserve shared metadata");
    for (int stale_value = 9901; stale_value <= 9911; ++stale_value) {
        check_not_contains(output_worksheet_xml, "<v>" + std::to_string(stale_value) + "</v>",
            "flushed WorksheetEditor office-like shared formula sheet should drop stale cached values");
    }
    check(fastxlsx::test::read_zip_entries(source).at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "office-like shared formula rewrite should not mutate untouched source sheet bytes");
}

void test_public_worksheet_editor_materializes_array_and_datatable_formula_metadata()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-array-datatable-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-array-datatable-formula-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1"><f t="array" ref="A1:B1" aca="1" ca="1" bx="1">SUM(B1:C1)</f><v>123</v></c>)"
        R"(<c r="B1"><f t="array" ref="A1:B1"/><v>456</v></c>)"
        R"(<c r="C1"><f t="dataTable" ref="C1:D1" dt2D="1" dtr="1" del1="0" del2="0" r1="A1" r2="B1">A1+1</f><v>789</v></c>)"
        R"(<c r="D1"><f t="dataTable" ref="C1:D1" ca="1"/><v>321</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> array_formula = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> array_cached = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> datatable_formula = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> datatable_cached = sheet.try_cell("D1");
    check(array_formula.has_value() && array_formula->kind() == fastxlsx::CellValueKind::Formula
            && array_formula->text_value() == "SUM(B1:C1)",
        "WorksheetEditor should flatten source array formula text to plain formula");
    check(array_cached.has_value() && array_cached->kind() == fastxlsx::CellValueKind::Number
            && array_cached->number_value() == 456.0,
        "WorksheetEditor should use cached scalar fallback for metadata-only array formulas");
    check(datatable_formula.has_value()
            && datatable_formula->kind() == fastxlsx::CellValueKind::Formula
            && datatable_formula->text_value() == "A1+1",
        "WorksheetEditor should flatten source dataTable formula text to plain formula");
    check(datatable_cached.has_value() && datatable_cached->kind() == fastxlsx::CellValueKind::Number
            && datatable_cached->number_value() == 321.0,
        "WorksheetEditor should use cached scalar fallback for metadata-only dataTable formulas");
    check(!sheet.has_pending_changes(),
        "array/dataTable formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "array/dataTable formula read-only materialization should not dirty the workbook editor");

    sheet.set_cell("F2", fastxlsx::CellValue::text("array-datatable-edit"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml, R"(<c r="A1"><f>SUM(B1:C1)</f></c>)",
        "dirty projection should write array formula text as plain formula");
    check_contains(output_worksheet_xml, R"(<c r="B1"><v>456</v></c>)",
        "dirty projection should retain array metadata-only cached scalar fallback");
    check_contains(output_worksheet_xml, R"(<c r="C1"><f>A1+1</f></c>)",
        "dirty projection should write dataTable formula text as plain formula");
    check_contains(output_worksheet_xml, R"(<c r="D1"><v>321</v></c>)",
        "dirty projection should retain dataTable metadata-only cached scalar fallback");
    check_contains(output_worksheet_xml,
        R"(<c r="F2" t="inlineStr"><is><t>array-datatable-edit</t></is></c>)",
        "dirty projection should include later edits after array/dataTable materialization");
    check_not_contains(output_worksheet_xml, R"(t="array")",
        "dirty projection should not preserve array formula metadata");
    check_not_contains(output_worksheet_xml, R"(t="dataTable")",
        "dirty projection should not preserve dataTable formula metadata");
    check_not_contains(output_worksheet_xml, R"(ca="1")",
        "dirty projection should not preserve known formula calculation metadata");
    check_not_contains(output_worksheet_xml, R"(dt2D="1")",
        "dirty projection should not preserve dataTable formula attributes");
    check_not_contains(output_worksheet_xml, "<v>123</v>",
        "dirty projection should drop stale array formula cached values");
    check_not_contains(output_worksheet_xml, "<v>789</v>",
        "dirty projection should drop stale dataTable formula cached values");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_materializes_source_supported_values();
        test_public_worksheet_editor_materializes_source_scalar_string_cells();
        test_public_worksheet_editor_flattens_source_inline_rich_text();
        test_public_worksheet_editor_materializes_prefixed_source_inline_strings();
        test_public_worksheet_editor_materializes_source_default_style_attribute_as_unstyled();
        test_public_worksheet_editor_defers_source_shared_strings_until_index_cells();
        test_public_worksheet_editor_defers_duplicate_shared_strings_relationship_until_index_cells();
        test_public_worksheet_editor_defers_malformed_shared_strings_xml_until_index_cells();
        test_public_worksheet_editor_defers_wrong_shared_strings_content_type_until_index_cells();
        test_public_worksheet_editor_materializes_source_max_coordinate_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_formula_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_shared_string_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_scalar_values_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_empty_inline_strings_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_rich_shared_string_and_erases_edge();
        test_public_worksheet_editor_materializes_empty_source_worksheets();
        test_public_worksheet_editor_drops_source_wrapper_metadata_on_dirty_projection();
        test_public_worksheet_editor_drops_relationship_wrapper_metadata_without_pruning();
        test_public_worksheet_editor_drops_range_wrapper_metadata_on_dirty_projection();
        test_public_worksheet_editor_drops_source_comments_and_processing_instructions_on_dirty_projection();
        test_public_worksheet_editor_read_only_materialization_keeps_noop_save_as_copy_original();
        test_public_worksheet_editor_materializes_source_shared_strings();
        test_public_worksheet_editor_accepts_legal_source_shared_strings_xml_declarations();
        test_public_worksheet_editor_flattens_rich_source_shared_strings();
        test_public_worksheet_editor_materializes_prefixed_source_shared_strings();
        test_public_worksheet_editor_materializes_local_names_without_namespace_validation();
        test_public_worksheet_editor_materializes_source_shared_strings_xml_space_and_projects_inline();
        test_public_worksheet_editor_ignores_source_shared_strings_counts_and_unknown_attributes();
        test_public_worksheet_editor_materializes_source_formulas();
        test_public_worksheet_editor_materializes_source_error_cells();
        test_public_worksheet_editor_ignores_formula_cached_result_types();
        test_public_worksheet_editor_materializes_source_shared_formulas();
        test_public_worksheet_editor_materializes_source_order_shared_formula_matrix();
        test_public_worksheet_editor_materializes_office_like_shared_formula_shape();
        test_public_worksheet_editor_materializes_array_and_datatable_formula_metadata();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor source-success check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor source-success tests passed\n");
    return 0;
}