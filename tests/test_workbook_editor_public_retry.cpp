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
    return shard == "all" || shard == "public-retry";
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

void test_public_worksheet_editor_rename_back_failed_mutation_preserves_clean_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-mutation-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-mutation-output.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientFailure");
    editor.rename_sheet("TransientFailure", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes(),
        "rename-back materialized session should start clean");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("rename-back-invalid"));
    }), "invalid A1 mutation after rename-back should throw");

    const std::optional<std::string> failed_mutation_error =
        editor.last_edit_error();
    check(failed_mutation_error.has_value(),
        "failed rename-back materialized mutation should set last_edit_error");
    check(!sheet.has_pending_changes(),
        "failed mutation after rename-back should preserve clean dirty state");
    check(editor.pending_change_count() == 2,
        "failed mutation after rename-back should not count a materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed mutation after rename-back should not add dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "failed mutation after rename-back should not add dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed mutation after rename-back should not add dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "failed mutation after rename-back should preserve empty current summaries");
    check(editor.has_worksheet("Data") && !editor.has_worksheet("TransientFailure"),
        "failed mutation after rename-back should preserve restored planned catalog");

    sheet.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-recovered-after-failure"));
    check(!editor.last_edit_error().has_value(),
        "successful recovery mutation after rename-back should clear last_edit_error");
    check(sheet.has_pending_changes(),
        "successful recovery mutation after rename-back should dirty the session");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "recovered rename-back diagnostics should use the restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "recovered mutation after rename-back should create one current summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "recovered rename-back summary should use restored source/planned names");
            check(!summary.renamed,
                "recovered rename-back summary should not be marked renamed");
            check(summary.materialized_dirty,
                "recovered rename-back summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "recovered rename-back summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "save_as after recovered rename-back mutation should clear dirty state");
    check(editor.pending_change_count() == 3,
        "save_as after recovered rename-back mutation should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after recovered rename-back mutation should clear dirty names");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after recovered rename-back mutation should clear current summaries");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "recovered rename-back output should keep the restored source name");
    check_not_contains(output_entries.at("xl/workbook.xml"), "TransientFailure",
        "recovered rename-back output should not leak the transient planned name");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "rename-back-invalid",
        "failed rename-back mutation payload should not leak into output");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-recovered-after-failure",
        "recovered rename-back mutation should persist after save_as");
}

void test_public_worksheet_editor_rename_back_failed_save_as_preserves_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-output.xlsx");

    const auto source_entries_before = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientSave");
    editor.rename_sheet("TransientSave", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-dirty-before-failed-save"));

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();

    check(sheet.has_pending_changes(),
        "rename-back failed-save setup should leave the borrowed session dirty");
    check(editor.pending_change_count() == 2,
        "rename-back failed-save setup should count only the two rename calls before save");
    check(!editor.last_edit_error().has_value(),
        "rename-back failed-save setup should start without last_edit_error");

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as over source after rename-back dirty edit should fail before auto-flush");

    const auto source_entries_after = fastxlsx::test::read_zip_entries(source);
    check(source_entries_after == source_entries_before,
        "rejected source-overwrite save_as after rename-back should not mutate source package bytes");
    check(sheet.has_pending_changes(),
        "rejected save_as after rename-back should keep the borrowed session dirty");
    check(editor.pending_change_count() == 2,
        "rejected save_as after rename-back should not count a materialized handoff");
    check(!editor.last_edit_error().has_value(),
        "rejected save_as after rename-back should not create last_edit_error");
    check(editor.has_worksheet("Data") && !editor.has_worksheet("TransientSave"),
        "rejected save_as after rename-back should preserve restored planned catalog");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "rejected save_as after rename-back should preserve restored dirty name");
    }
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "rejected save_as after rename-back should preserve materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "rejected save_as after rename-back should preserve materialized memory");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "rejected save_as after rename-back should preserve one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "rejected save_as rename-back summary should use restored names");
            check(!summary.renamed,
                "rejected save_as rename-back summary should not remain marked renamed");
            check(!summary.sheet_data_replaced,
                "rejected save_as rename-back summary should not invent replacement diagnostics");
            check(summary.materialized_dirty,
                "rejected save_as rename-back summary should keep materialized dirty flag");
            check(summary.materialized_cell_count == dirty_cell_count,
                "rejected save_as rename-back summary should preserve cell count");
            check(summary.estimated_materialized_memory_usage == dirty_memory_usage,
                "rejected save_as rename-back summary should preserve memory estimate");
        }
    }

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "safe save_as after rename-back rejection should flush dirty state");
    check(editor.pending_change_count() == 3,
        "safe save_as after rename-back rejection should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "safe save_as after rename-back rejection should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "safe save_as after rename-back rejection should clear dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "safe save_as after rename-back rejection should clear dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "safe save_as after rename-back rejection should clear current summaries");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "rename-back failed-save recovery output should keep the restored source name");
    check_not_contains(output_entries.at("xl/workbook.xml"), "TransientSave",
        "rename-back failed-save recovery output should not leak the transient planned name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-dirty-before-failed-save",
        "rename-back failed-save recovery output should include the materialized edit");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "rename-back failed-save recovery output should replace the old source value");
}

void test_public_worksheet_editor_rename_back_failed_save_as_reacquire_reuses_saved_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-reacquire-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-reacquire-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-reacquire-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientReacquire");
    editor.rename_sheet("TransientReacquire", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-reacquire-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before rename-back reacquire setup flushes");
    editor.save_as(first_output);

    check(!sheet.has_pending_changes(),
        "safe save_as after rename-back failed save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "safe save_as after rename-back failed save should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "safe save_as before reacquire should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "safe save_as before reacquire should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "safe save_as before reacquire should clear dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "safe save_as before reacquire should clear rename-back dirty summaries");
    check(!editor.last_edit_error().has_value(),
        "failed save_as plus safe save_as should not create last_edit_error");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!reacquired.has_pending_changes(),
        "matching reacquire after rename-back failed-save recovery should start clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-reacquire-first",
        "matching reacquire after rename-back failed-save recovery should reuse saved materialized state");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean reacquire after rename-back failed-save recovery should not dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "clean reacquire after rename-back failed-save recovery should not dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "clean reacquire after rename-back failed-save recovery should not dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "clean reacquire after rename-back failed-save recovery should keep summaries empty");

    reacquired.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-reacquire-second"));
    check(sheet.has_pending_changes(),
        "post-reacquire mutation should dirty the shared materialized session visible to older handle");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "post-reacquire rename-back dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "post-reacquire rename-back mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "post-reacquire rename-back summary should use restored names");
            check(!summary.renamed,
                "post-reacquire rename-back summary should not be marked renamed");
            check(summary.materialized_dirty,
                "post-reacquire rename-back summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "post-reacquire rename-back summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean both borrowed handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the second materialized handoff");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear rename-back reacquire summaries");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value that reacquire must not reload");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-first",
        "source package should not contain the saved materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first rename-back reacquire output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientReacquire",
        "first rename-back reacquire output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-first",
        "first output should include the saved materialized value");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-second",
        "first output should not include the later post-reacquire edit");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second rename-back reacquire output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientReacquire",
        "second rename-back reacquire output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-first",
        "second output should preserve the saved materialized value after reacquire");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-second",
        "second output should include the post-reacquire mutation");
}

void test_public_worksheet_editor_rename_back_failed_save_as_option_mismatch_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;
    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 9;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientOptionMismatch");
    editor.rename_sheet("TransientOptionMismatch", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-option-mismatch-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before option-mismatch recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before option mismatch should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-option-mismatch-first",
        "matching reacquire before option mismatch should reuse saved materialized state");
    check(editor.pending_change_count() == 3,
        "safe save before option mismatch should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "safe save before option mismatch should leave dirty names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "safe save before option mismatch should leave dirty cell count empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "safe save before option mismatch should leave dirty memory empty");
    check(editor.pending_worksheet_edits().empty(),
        "safe save before option mismatch should leave dirty summaries empty");
    check(!editor.last_edit_error().has_value(),
        "rename-back failed-save recovery should not create last_edit_error");

    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("Data", mismatched_options);
    }), "try_worksheet should reject mismatched options after failed-save recovery");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data", mismatched_options);
    }), "worksheet should reject mismatched options after failed-save recovery");

    check(!editor.last_edit_error().has_value(),
        "post-recovery option mismatch should not update last_edit_error");
    check(editor.pending_change_count() == 3,
        "post-recovery option mismatch should not queue another public edit");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "post-recovery option mismatch should keep existing handles clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-recovery option mismatch should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "post-recovery option mismatch should keep dirty cell count clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-recovery option mismatch should keep dirty memory clear");
    check(editor.pending_worksheet_edits().empty(),
        "post-recovery option mismatch should keep summaries empty");
    check(editor.has_worksheet("Data") &&
            !editor.has_worksheet("TransientOptionMismatch"),
        "post-recovery option mismatch should preserve the restored planned catalog name");

    const fastxlsx::CellValue preserved_value = reacquired.get_cell(1, 1);
    check(preserved_value.kind() == fastxlsx::CellValueKind::Text &&
            preserved_value.text_value() == "rename-back-option-mismatch-first",
        "post-recovery option mismatch should preserve the saved materialized value");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after post-recovery option mismatch should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-option-mismatch-first",
        "matching reacquire after option mismatch should still use the saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-option-mismatch-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "valid post-mismatch mutation should dirty older shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-mismatch dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-mismatch mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-mismatch summary should use restored names");
            check(!summary.renamed,
                "valid post-mismatch summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-mismatch summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-mismatch summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all option-mismatch recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear dirty names after option mismatch");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear dirty cell count after option mismatch");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear dirty memory after option mismatch");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after option mismatch");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first option-mismatch recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientOptionMismatch",
        "first option-mismatch recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-first",
        "first output should contain the saved value before option mismatch");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-second",
        "first output should not contain the later post-mismatch mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second option-mismatch recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientOptionMismatch",
        "second option-mismatch recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-first",
        "second output should preserve the saved value after option mismatch");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-second",
        "second output should include the valid post-mismatch mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after option mismatch");
}

void test_public_worksheet_editor_rename_back_failed_save_as_missing_try_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMissingTry");
    editor.rename_sheet("TransientMissingTry", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-missing-try-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before missing-try recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before missing try_worksheet should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-missing-try-first",
        "matching reacquire before missing try_worksheet should reuse saved materialized state");

    const std::optional<fastxlsx::WorksheetEditor> missing_transient =
        editor.try_worksheet("TransientMissingTry", options);
    check(!missing_transient.has_value(),
        "try_worksheet should return empty for the old transient planned name");
    const std::optional<fastxlsx::WorksheetEditor> missing =
        editor.try_worksheet("Missing", options);
    check(!missing.has_value(),
        "try_worksheet should return empty for a missing name after failed-save recovery");

    check(!editor.last_edit_error().has_value(),
        "post-recovery missing try_worksheet should not update last_edit_error");
    check(editor.pending_change_count() == 3,
        "post-recovery missing try_worksheet should not queue another public edit");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "post-recovery missing try_worksheet should keep existing handles clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-recovery missing try_worksheet should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "post-recovery missing try_worksheet should keep dirty cell count clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-recovery missing try_worksheet should keep dirty memory clear");
    check(editor.pending_worksheet_edits().empty(),
        "post-recovery missing try_worksheet should keep summaries empty");
    check(editor.has_worksheet("Data") && !editor.has_worksheet("TransientMissingTry"),
        "post-recovery missing try_worksheet should preserve the restored planned catalog name");

    const fastxlsx::CellValue preserved_value = reacquired.get_cell(1, 1);
    check(preserved_value.kind() == fastxlsx::CellValueKind::Text &&
            preserved_value.text_value() == "rename-back-missing-try-first",
        "post-recovery missing try_worksheet should preserve the saved materialized value");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after missing try_worksheet should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-missing-try-first",
        "matching reacquire after missing try_worksheet should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-missing-try-second"));
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-missing-try dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-missing-try mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-missing-try summary should use restored names");
            check(!summary.renamed,
                "valid post-missing-try summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-missing-try summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-missing-try summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all missing-try recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after missing try_worksheet");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after missing try_worksheet");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first missing-try recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMissingTry",
        "first missing-try recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-first",
        "first output should contain the saved value before missing try_worksheet");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-second",
        "first output should not contain the later post-missing-try mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second missing-try recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientMissingTry",
        "second missing-try recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-first",
        "second output should preserve the saved value after missing try_worksheet");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-second",
        "second output should include the valid post-missing-try mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after missing try_worksheet");
}

void test_public_worksheet_editor_rename_back_failed_save_as_missing_worksheet_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMissingWorksheet");
    editor.rename_sheet("TransientMissingWorksheet", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-missing-worksheet-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before missing-worksheet recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before missing worksheet should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-missing-worksheet-first",
        "matching reacquire before missing worksheet should reuse saved materialized state");

    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("TransientMissingWorksheet", options);
    }), "worksheet should throw for the old transient planned name");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Missing", options);
    }), "worksheet should throw for a missing name after failed-save recovery");

    check(!editor.last_edit_error().has_value(),
        "post-recovery missing worksheet should not update last_edit_error");
    check(editor.pending_change_count() == 3,
        "post-recovery missing worksheet should not queue another public edit");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "post-recovery missing worksheet should keep existing handles clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-recovery missing worksheet should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "post-recovery missing worksheet should keep dirty cell count clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-recovery missing worksheet should keep dirty memory clear");
    check(editor.pending_worksheet_edits().empty(),
        "post-recovery missing worksheet should keep summaries empty");
    check(editor.has_worksheet("Data") &&
            !editor.has_worksheet("TransientMissingWorksheet"),
        "post-recovery missing worksheet should preserve the restored planned catalog name");

    const fastxlsx::CellValue preserved_value = reacquired.get_cell(1, 1);
    check(preserved_value.kind() == fastxlsx::CellValueKind::Text &&
            preserved_value.text_value() == "rename-back-missing-worksheet-first",
        "post-recovery missing worksheet should preserve the saved materialized value");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after missing worksheet should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-missing-worksheet-first",
        "matching reacquire after missing worksheet should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-missing-worksheet-second"));
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-missing-worksheet dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-missing-worksheet mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-missing-worksheet summary should use restored names");
            check(!summary.renamed,
                "valid post-missing-worksheet summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-missing-worksheet summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-missing-worksheet summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all missing-worksheet recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after missing worksheet");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after missing worksheet");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first missing-worksheet recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMissingWorksheet",
        "first missing-worksheet recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-first",
        "first output should contain the saved value before missing worksheet");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-second",
        "first output should not contain the later post-missing-worksheet mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second missing-worksheet recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientMissingWorksheet",
        "second missing-worksheet recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-first",
        "second output should preserve the saved value after missing worksheet");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-second",
        "second output should include the valid post-missing-worksheet mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after missing worksheet");
}

void test_public_worksheet_editor_rename_back_failed_save_as_catalog_queries_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientCatalogQuery");
    editor.rename_sheet("TransientCatalogQuery", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-catalog-query-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before catalog-query recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before catalog queries should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-catalog-query-first",
        "matching reacquire before catalog queries should reuse saved materialized state");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    {
        const std::vector<std::string> names = editor.worksheet_names();
        check(names.size() == 2 && names[0] == "Data" && names[1] == "Untouched",
            "planned catalog query after recovery should report restored names");
    }
    check(editor.has_worksheet("Data"),
        "planned catalog query after recovery should find restored Data");
    check(editor.has_worksheet("Untouched"),
        "planned catalog query after recovery should find untouched sheet");
    check(!editor.has_worksheet("TransientCatalogQuery"),
        "planned catalog query after recovery should not revive transient name");
    check(!editor.has_worksheet("Missing"),
        "planned catalog query after recovery should reject absent names");

    {
        const std::vector<std::string> source_names = editor.source_worksheet_names();
        check(source_names.size() == 2 && source_names[0] == "Data" &&
                source_names[1] == "Untouched",
            "source catalog query after recovery should report original names");
    }
    check(editor.has_source_worksheet("Data"),
        "source catalog query after recovery should find source Data");
    check(editor.has_source_worksheet("Untouched"),
        "source catalog query after recovery should find source untouched sheet");
    check(!editor.has_source_worksheet("TransientCatalogQuery"),
        "source catalog query after recovery should not expose transient planned name");
    check(!editor.has_source_worksheet("Missing"),
        "source catalog query after recovery should reject absent source names");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-catalog-query-first",
        "TransientCatalogQuery",
        "post-recovery catalog queries",
        3);

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after catalog queries should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-catalog-query-first",
        "matching reacquire after catalog queries should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-catalog-query-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-catalog-query mutation should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-catalog-query dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-catalog-query mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-catalog-query summary should use restored names");
            check(!summary.renamed,
                "valid post-catalog-query summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-catalog-query summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-catalog-query summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all catalog-query recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after catalog queries");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after catalog queries");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after catalog queries");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-first",
        "source package should not contain the saved catalog-query materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first catalog-query recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientCatalogQuery",
        "first catalog-query recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-first",
        "first output should contain the saved value before catalog queries");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-second",
        "first output should not contain the later post-catalog-query mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second catalog-query recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientCatalogQuery",
        "second catalog-query recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-first",
        "second output should preserve the saved value after catalog queries");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-second",
        "second output should include the valid post-catalog-query mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after catalog queries");
}

void test_public_worksheet_editor_rename_back_failed_save_as_diagnostics_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientDiagnostics");
    editor.rename_sheet("TransientDiagnostics", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-diagnostics-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before diagnostic-query recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before diagnostics should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-diagnostics-first",
        "matching reacquire before diagnostics should reuse saved materialized state");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    check(editor.has_pending_changes(),
        "post-save recovery should still expose prior public edits as pending facade state");
    check(editor.pending_change_count() == 3,
        "post-save recovery should count rename, rename-back, and materialized handoff");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "post-save recovery should not invent replacement diagnostics");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-save recovery should start with clean materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "post-save recovery should start with clean materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-save recovery should start with clean materialized memory");
    check(!editor.has_pending_replacement("Data") &&
            !editor.has_pending_replacement("TransientDiagnostics") &&
            !editor.has_pending_replacement("Missing"),
        "post-save recovery should not report replacement payloads");
    check(editor.pending_worksheet_edits().empty(),
        "post-save recovery should not expose dirty materialized summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "post-save recovery catalog diagnostic should keep source workbook sheet count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "Data" && !catalog[0].renamed,
                "post-save recovery catalog diagnostic should show restored Data mapping");
            check(catalog[1].source_name == "Untouched" &&
                    catalog[1].planned_name == "Untouched" && !catalog[1].renamed,
                "post-save recovery catalog diagnostic should preserve untouched mapping");
        }
    }
    check(!editor.last_edit_error().has_value(),
        "post-save recovery should start diagnostics with no last_edit_error");

    check_public_inspection_preserves_last_edit_error(editor, editor.last_edit_error());
    (void)editor.has_pending_replacement("TransientDiagnostics");
    (void)editor.has_pending_replacement("Missing");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-diagnostics-first",
        "TransientDiagnostics",
        "post-recovery diagnostic queries",
        3);

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after diagnostics should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-diagnostics-first",
        "matching reacquire after diagnostics should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-diagnostics-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-diagnostic-query mutation should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-diagnostic-query dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-diagnostic-query mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-diagnostic-query summary should use restored names");
            check(!summary.renamed,
                "valid post-diagnostic-query summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-diagnostic-query summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-diagnostic-query summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all diagnostic-query recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after diagnostics");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after diagnostics");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-first",
        "source package should not contain the saved diagnostic-query materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first diagnostic-query recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientDiagnostics",
        "first diagnostic-query recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-first",
        "first output should contain the saved value before diagnostics");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-second",
        "first output should not contain the later post-diagnostic-query mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second diagnostic-query recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientDiagnostics",
        "second diagnostic-query recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-first",
        "second output should preserve the saved value after diagnostics");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-second",
        "second output should include the valid post-diagnostic-query mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after diagnostics");
}

void test_public_worksheet_editor_rename_back_failed_save_as_handle_reads_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-handle-reads-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-handle-reads-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-handle-reads-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientHandleReads");
    editor.rename_sheet("TransientHandleReads", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-handle-reads-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before handle-read recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before handle reads should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const std::optional<fastxlsx::CellValue> maybe_saved = sheet.try_cell(1, 1);
    check(maybe_saved.has_value() &&
            maybe_saved->kind() == fastxlsx::CellValueKind::Text &&
            maybe_saved->text_value() == "rename-back-handle-reads-first",
        "post-recovery try_cell should read the saved materialized value");
    const std::optional<fastxlsx::CellValue> maybe_saved_a1 =
        reacquired.try_cell("A1");
    check(maybe_saved_a1.has_value() &&
            maybe_saved_a1->kind() == fastxlsx::CellValueKind::Text &&
            maybe_saved_a1->text_value() == "rename-back-handle-reads-first",
        "post-recovery A1 try_cell should read the saved materialized value");
    check(!reacquired.try_cell(9, 9).has_value(),
        "post-recovery try_cell should not synthesize missing cells");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(9, 9); }),
        "post-recovery get_cell should still throw for missing cells");

    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-handle-reads-first",
        "post-recovery get_cell should preserve the saved materialized value");
    const fastxlsx::CellValue source_value = sheet.get_cell("A2");
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a2",
        "post-recovery get_cell should keep unchanged source-backed cells");
    check(sheet.cell_count() == 3 && reacquired.cell_count() == 3,
        "post-recovery cell_count should report the clean saved sparse store");
    check(sheet.estimated_memory_usage() > 0 &&
            sheet.estimated_memory_usage() == reacquired.estimated_memory_usage(),
        "post-recovery estimated_memory_usage should read the shared saved store");

    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
            reacquired.sparse_cells();
        check(cells.size() == 3,
            "post-recovery sparse_cells should snapshot the saved sparse store");
        if (cells.size() == 3) {
            check(cells[0].reference.row == 1 && cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[0].value.text_value() == "rename-back-handle-reads-first",
                "post-recovery sparse_cells should expose saved A1 first");
            check(cells[1].reference.row == 1 && cells[1].reference.column == 2 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    cells[1].value.number_value() == 1.0,
                "post-recovery sparse_cells should preserve source-backed B1");
            check(cells[2].reference.row == 2 && cells[2].reference.column == 1 &&
                    cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[2].value.text_value() == "placeholder-a2",
                "post-recovery sparse_cells should preserve source-backed A2");
        }
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 2});
        check(range_cells.size() == 2,
            "post-recovery sparse_cells(range) should snapshot only requested records");
        if (range_cells.size() == 2) {
            check(range_cells[0].reference.row == 1 &&
                    range_cells[0].reference.column == 1 &&
                    range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    range_cells[0].value.text_value() == "rename-back-handle-reads-first",
                "post-recovery sparse_cells(range) should expose saved A1");
            check(range_cells[1].reference.row == 1 &&
                    range_cells[1].reference.column == 2 &&
                    range_cells[1].value.kind() == fastxlsx::CellValueKind::Number,
                "post-recovery sparse_cells(range) should expose source-backed B1");
        }
    }

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-handle-reads-first",
        "TransientHandleReads",
        "post-recovery handle reads",
        3);

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after handle reads should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-handle-reads-first",
        "matching reacquire after handle reads should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-handle-reads-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-handle-read mutation should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-handle-read dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-handle-read mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-handle-read summary should use restored names");
            check(!summary.renamed,
                "valid post-handle-read summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-handle-read summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-handle-read summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all handle-read recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after handle reads");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after handle reads");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after handle reads");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-first",
        "source package should not contain the saved handle-read materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first handle-read recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientHandleReads",
        "first handle-read recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-first",
        "first output should contain the saved value before handle reads");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-second",
        "first output should not contain the later post-handle-read mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second handle-read recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientHandleReads",
        "second handle-read recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-first",
        "second output should preserve the saved value after handle reads");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-second",
        "second output should include the valid post-handle-read mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after handle reads");
}

void test_public_worksheet_editor_rename_back_failed_save_as_invalid_reads_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-reads-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-reads-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-reads-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientInvalidReads");
    editor.rename_sheet("TransientInvalidReads", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-invalid-reads-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before invalid-read recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before invalid reads should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired invalid-read handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const fastxlsx::CellValue saved_before_invalid_reads = reacquired.get_cell(1, 1);
    check(saved_before_invalid_reads.kind() == fastxlsx::CellValueKind::Text &&
            saved_before_invalid_reads.text_value() == "rename-back-invalid-reads-first",
        "reacquired invalid-read setup should expose the saved materialized value");
    check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Text &&
            sheet.get_cell("A2").text_value() == "placeholder-a2",
        "invalid-read setup should preserve unchanged source-backed cells");

    const std::size_t baseline_count = reacquired.cell_count();
    const std::size_t baseline_memory = reacquired.estimated_memory_usage();
    check(baseline_count == 3 && baseline_memory > 0,
        "invalid-read setup should start from the saved sparse store");
    check(!editor.last_edit_error().has_value(),
        "invalid-read setup should start without mutation diagnostics");

    check(threw_fastxlsx_error([&] { (void)reacquired.try_cell(0, 1); }),
        "post-recovery invalid read should reject row zero");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "post-recovery invalid read should reject column zero");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(1048577, 1); }),
        "post-recovery invalid read should reject rows beyond Excel limits");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(1, 16385); }),
        "post-recovery invalid read should reject columns beyond Excel limits");
    check(threw_fastxlsx_error([&] { (void)reacquired.try_cell("a1"); }),
        "post-recovery invalid A1 read should reject lowercase references");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("A1:B2"); }),
        "post-recovery invalid A1 read should reject range references");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("A01"); }),
        "post-recovery invalid A1 read should reject leading-zero rows");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell("XFE1"); }),
        "post-recovery invalid A1 read should reject columns beyond Excel limits");
    check(threw_fastxlsx_error([&] {
        (void)reacquired.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "post-recovery invalid range read should reject row zero");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {2, 1, 1, 1});
    }), "post-recovery invalid range read should reject reversed ranges");

    check(reacquired.cell_count() == baseline_count &&
            sheet.cell_count() == baseline_count,
        "post-recovery invalid reads should not mutate sparse cell counts");
    check(reacquired.estimated_memory_usage() == baseline_memory &&
            sheet.estimated_memory_usage() == baseline_memory,
        "post-recovery invalid reads should not change sparse memory estimates");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-invalid-reads-first",
        "TransientInvalidReads",
        "post-recovery invalid reads",
        3);

    const fastxlsx::CellValue unchanged_after_invalid_reads = sheet.get_cell("A2");
    check(unchanged_after_invalid_reads.kind() == fastxlsx::CellValueKind::Text &&
            unchanged_after_invalid_reads.text_value() == "placeholder-a2",
        "post-recovery invalid reads should preserve unchanged source-backed cells");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after invalid reads should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-invalid-reads-first",
        "matching reacquire after invalid reads should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-invalid-reads-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-invalid-read mutation should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-invalid-read dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-invalid-read mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-invalid-read summary should use restored names");
            check(!summary.renamed,
                "valid post-invalid-read summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-invalid-read summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-invalid-read summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all invalid-read recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after invalid reads");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after invalid reads");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after invalid reads");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-first",
        "source package should not contain the saved invalid-read materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first invalid-read recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientInvalidReads",
        "first invalid-read recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-first",
        "first output should contain the saved value before invalid reads");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-second",
        "first output should not contain the later post-invalid-read mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second invalid-read recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientInvalidReads",
        "second invalid-read recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-first",
        "second output should preserve the saved value after invalid reads");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-second",
        "second output should include the valid post-invalid-read mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after invalid reads");
}

void test_public_worksheet_editor_rename_back_failed_save_as_invalid_mutations_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-mutations-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-mutations-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-mutations-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientInvalidMutations");
    editor.rename_sheet("TransientInvalidMutations", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-invalid-mutations-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before invalid-mutation recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before invalid mutations should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired invalid-mutation handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const fastxlsx::CellValue saved_before_invalid_mutations =
        reacquired.get_cell(1, 1);
    check(saved_before_invalid_mutations.kind() == fastxlsx::CellValueKind::Text &&
            saved_before_invalid_mutations.text_value() ==
                "rename-back-invalid-mutations-first",
        "reacquired invalid-mutation setup should expose the saved materialized value");
    const fastxlsx::CellValue source_backed_before_invalid_mutations =
        sheet.get_cell("A2");
    check(source_backed_before_invalid_mutations.kind() ==
                fastxlsx::CellValueKind::Text &&
            source_backed_before_invalid_mutations.text_value() == "placeholder-a2",
        "invalid-mutation setup should preserve unchanged source-backed cells");

    const std::size_t baseline_count = reacquired.cell_count();
    const std::size_t baseline_memory = reacquired.estimated_memory_usage();
    check(baseline_count == 3 && baseline_memory > 0,
        "invalid-mutation setup should start from the saved sparse store");
    check(!editor.last_edit_error().has_value(),
        "invalid-mutation setup should start without mutation diagnostics");

    check(threw_fastxlsx_error([&] {
        reacquired.set_cell(0, 1,
            fastxlsx::CellValue::text("invalid-mutation-row-zero"));
    }), "post-recovery invalid mutation should reject row zero");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("a1",
            fastxlsx::CellValue::text("invalid-mutation-lowercase"));
    }), "post-recovery invalid mutation should reject lowercase references");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("XFE1",
            fastxlsx::CellValue::text("invalid-mutation-column-overflow"));
    }), "post-recovery invalid mutation should reject overflow columns");
    check(threw_fastxlsx_error([&] { sheet.erase_cell(1048577, 1); }),
        "post-recovery invalid erase should reject rows beyond Excel limits");
    check(threw_fastxlsx_error([&] { reacquired.erase_cell("A1:B2"); }),
        "post-recovery invalid erase should reject range references");

    const std::optional<std::string> invalid_mutation_error = editor.last_edit_error();
    check(invalid_mutation_error.has_value(),
        "post-recovery invalid mutations should update last_edit_error");
    check(reacquired.cell_count() == baseline_count &&
            sheet.cell_count() == baseline_count,
        "post-recovery invalid mutations should not mutate sparse cell counts");
    check(reacquired.estimated_memory_usage() == baseline_memory &&
            sheet.estimated_memory_usage() == baseline_memory,
        "post-recovery invalid mutations should not change sparse memory estimates");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-invalid-mutations-first",
        "TransientInvalidMutations",
        "post-recovery invalid mutations",
        3,
        invalid_mutation_error);

    const fastxlsx::CellValue unchanged_after_invalid_mutations = sheet.get_cell("A2");
    check(unchanged_after_invalid_mutations.kind() == fastxlsx::CellValueKind::Text &&
            unchanged_after_invalid_mutations.text_value() == "placeholder-a2",
        "post-recovery invalid mutations should preserve unchanged source-backed cells");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after invalid mutations should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-invalid-mutations-first",
        "matching reacquire after invalid mutations should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-invalid-mutations-second"));
    check(!editor.last_edit_error().has_value(),
        "valid post-invalid-mutation edit should clear mutation diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-invalid-mutation edit should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-invalid-mutation dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-invalid-mutation edit should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-invalid-mutation summary should use restored names");
            check(!summary.renamed,
                "valid post-invalid-mutation summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-invalid-mutation summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-invalid-mutation summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all invalid-mutation recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after invalid mutations");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after invalid mutations");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after invalid mutations");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-first",
        "source package should not contain the saved invalid-mutation materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first invalid-mutation recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientInvalidMutations",
        "first invalid-mutation recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-first",
        "first output should contain the saved value before invalid mutations");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-second",
        "first output should not contain the later post-invalid-mutation edit");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-row-zero",
        "first output should not contain rejected invalid row payload");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-lowercase",
        "first output should not contain rejected invalid A1 payload");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-column-overflow",
        "first output should not contain rejected invalid column payload");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second invalid-mutation recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientInvalidMutations",
        "second invalid-mutation recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-first",
        "second output should preserve the saved value after invalid mutations");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-second",
        "second output should include the valid post-invalid-mutation edit");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-row-zero",
        "second output should not contain rejected invalid row payload");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-lowercase",
        "second output should not contain rejected invalid A1 payload");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-column-overflow",
        "second output should not contain rejected invalid column payload");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after invalid mutations");
}

void test_public_worksheet_editor_rename_back_failed_save_as_missing_erase_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-erase-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-erase-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-erase-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMissingErase");
    editor.rename_sheet("TransientMissingErase", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-missing-erase-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before missing-erase recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before missing erase should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired missing-erase handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const fastxlsx::CellValue saved_before_missing_erase = reacquired.get_cell(1, 1);
    check(saved_before_missing_erase.kind() == fastxlsx::CellValueKind::Text &&
            saved_before_missing_erase.text_value() == "rename-back-missing-erase-first",
        "reacquired missing-erase setup should expose the saved materialized value");
    const fastxlsx::CellValue source_backed_before_missing_erase =
        sheet.get_cell("A2");
    check(source_backed_before_missing_erase.kind() == fastxlsx::CellValueKind::Text &&
            source_backed_before_missing_erase.text_value() == "placeholder-a2",
        "missing-erase setup should preserve unchanged source-backed cells");

    const std::size_t baseline_count = reacquired.cell_count();
    const std::size_t baseline_memory = reacquired.estimated_memory_usage();
    check(baseline_count == 3 && baseline_memory > 0,
        "missing-erase setup should start from the saved sparse store");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("missing-erase-invalid"));
    }), "invalid mutation should seed last_edit_error before missing erase");
    check(editor.last_edit_error().has_value(),
        "invalid mutation before missing erase should update last_edit_error");

    reacquired.erase_cell(9, 9);
    sheet.erase_cell("D4");

    check(!editor.last_edit_error().has_value(),
        "post-recovery missing erase no-op should clear prior mutation diagnostics");
    check(reacquired.cell_count() == baseline_count &&
            sheet.cell_count() == baseline_count,
        "post-recovery missing erase no-op should not mutate sparse cell counts");
    check(reacquired.estimated_memory_usage() == baseline_memory &&
            sheet.estimated_memory_usage() == baseline_memory,
        "post-recovery missing erase no-op should not change sparse memory estimates");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-missing-erase-first",
        "TransientMissingErase",
        "post-recovery missing erase no-op",
        3);

    const fastxlsx::CellValue unchanged_after_missing_erase = sheet.get_cell("A2");
    check(unchanged_after_missing_erase.kind() == fastxlsx::CellValueKind::Text &&
            unchanged_after_missing_erase.text_value() == "placeholder-a2",
        "post-recovery missing erase no-op should preserve unchanged source-backed cells");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after missing erase should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-missing-erase-first",
        "matching reacquire after missing erase should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-missing-erase-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-missing-erase edit should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-missing-erase dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-missing-erase edit should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-missing-erase summary should use restored names");
            check(!summary.renamed,
                "valid post-missing-erase summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-missing-erase summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-missing-erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all missing-erase recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after missing erase");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after missing erase");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after missing erase");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-first",
        "source package should not contain the saved missing-erase materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first missing-erase recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMissingErase",
        "first missing-erase recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-first",
        "first output should contain the saved value before missing erase");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-second",
        "first output should not contain the later post-missing-erase edit");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "missing-erase-invalid",
        "first output should not contain rejected invalid mutation payload");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second missing-erase recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientMissingErase",
        "second missing-erase recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-first",
        "second output should preserve the saved value after missing erase");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-second",
        "second output should include the valid post-missing-erase edit");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "missing-erase-invalid",
        "second output should not contain rejected invalid mutation payload");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after missing erase");
}

void test_public_worksheet_editor_rename_back_failed_save_as_blank_and_existing_erase_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-blank-erase-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-blank-erase-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-blank-erase-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientBlankErase");
    editor.rename_sheet("TransientBlankErase", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-blank-erase-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before blank/erase recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before blank/erase should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired blank/erase handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const fastxlsx::CellValue saved_before_blank_erase = reacquired.get_cell(1, 1);
    check(saved_before_blank_erase.kind() == fastxlsx::CellValueKind::Text &&
            saved_before_blank_erase.text_value() == "rename-back-blank-erase-first",
        "reacquired blank/erase setup should expose the saved materialized value");
    const fastxlsx::CellValue source_backed_before_blank_erase = sheet.get_cell("A2");
    check(source_backed_before_blank_erase.kind() == fastxlsx::CellValueKind::Text &&
            source_backed_before_blank_erase.text_value() == "placeholder-a2",
        "blank/erase setup should preserve unchanged source-backed cells");
    check(!editor.last_edit_error().has_value(),
        "blank/erase setup should start without edit diagnostics");

    reacquired.set_cell("A1", fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);

    check(!editor.last_edit_error().has_value(),
        "post-recovery blank/erase mutations should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "post-recovery blank/erase mutations should dirty shared handles");
    check(reacquired.cell_count() == 2 && sheet.cell_count() == 2,
        "post-recovery blank/erase mutations should keep blank A1 and B1 only");
    {
        const fastxlsx::CellValue blank_a1 = sheet.get_cell("A1");
        check(blank_a1.kind() == fastxlsx::CellValueKind::Blank,
            "post-recovery explicit blank should be readable as CellValue::blank");
        check(!reacquired.try_cell(2, 1).has_value(),
            "post-recovery existing erase should remove source-backed A2");
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientBlankErase",
        "post-recovery blank/erase mutations",
        3,
        2,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean blank/erase recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the blank/erase materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear blank/erase dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear blank/erase dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear blank/erase dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after blank/erase");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after blank/erase");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "source package should still contain original A2 after blank/erase");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first blank/erase recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientBlankErase",
        "first blank/erase recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-blank-erase-first",
        "first output should contain the saved value before blank/erase");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "first output should still contain source-backed A2 before erase");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second blank/erase recovery output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientBlankErase",
        "second blank/erase recovery output should not leak the transient planned name");
    check_contains(second_worksheet_xml,
        R"(<row r="1"><c r="A1"/><c r="B1"><v>1</v></c></row>)",
        "second output should persist explicit blank A1 and preserve B1");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "second output should refresh dimension after existing source-cell erase");
    check_not_contains(second_worksheet_xml, "rename-back-blank-erase-first",
        "second output should remove prior text payload after explicit blank");
    check_not_contains(second_worksheet_xml, "placeholder-a1",
        "second output should not reload stale source A1 after blank");
    check_not_contains(second_worksheet_xml, "placeholder-a2",
        "second output should remove erased source-backed A2");
    check_not_contains(second_worksheet_xml, R"(<row r="2")",
        "second output should remove row 2 after erasing its only source cell");
}

void test_public_worksheet_editor_rename_back_failed_save_as_scalar_and_formula_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-scalar-formula-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-scalar-formula-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-scalar-formula-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientScalarFormula");
    editor.rename_sheet("TransientScalarFormula", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-scalar-formula-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before scalar/formula recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before scalar/formula edits should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired scalar/formula handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };
    check(!editor.last_edit_error().has_value(),
        "scalar/formula setup should start without edit diagnostics");
    {
        const fastxlsx::CellValue saved_a1 = sheet.get_cell("A1");
        check(saved_a1.kind() == fastxlsx::CellValueKind::Text &&
                saved_a1.text_value() == "rename-back-scalar-formula-first",
            "scalar/formula setup should expose the saved materialized A1 value");
        const fastxlsx::CellValue preserved_a2 = reacquired.get_cell(2, 1);
        check(preserved_a2.kind() == fastxlsx::CellValueKind::Text &&
                preserved_a2.text_value() == "placeholder-a2",
            "scalar/formula setup should preserve unchanged source-backed A2");
    }

    reacquired.set_cell(1, 1, fastxlsx::CellValue::number(42.25));
    sheet.set_cell("A2", fastxlsx::CellValue::boolean(true));
    reacquired.set_cell(3, 3,
        fastxlsx::CellValue::formula(R"(SUM(A1:B1)&"<ok>")"));

    check(!editor.last_edit_error().has_value(),
        "post-recovery scalar/formula mutations should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "post-recovery scalar/formula mutations should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "post-recovery scalar/formula mutations should keep A1, B1, A2, and C3");
    {
        const fastxlsx::CellValue number_a1 = sheet.get_cell("A1");
        const fastxlsx::CellValue preserved_b1 = reacquired.get_cell(1, 2);
        const fastxlsx::CellValue boolean_a2 = reacquired.get_cell("A2");
        const fastxlsx::CellValue formula_c3 = sheet.get_cell(3, 3);
        check(number_a1.kind() == fastxlsx::CellValueKind::Number &&
                number_a1.number_value() == 42.25,
            "post-recovery scalar/formula edits should read A1 as a number");
        check(preserved_b1.kind() == fastxlsx::CellValueKind::Number &&
                preserved_b1.number_value() == 1.0,
            "post-recovery scalar/formula edits should preserve source-backed B1");
        check(boolean_a2.kind() == fastxlsx::CellValueKind::Boolean &&
                boolean_a2.boolean_value(),
            "post-recovery scalar/formula edits should read A2 as true");
        check(formula_c3.kind() == fastxlsx::CellValueKind::Formula &&
                formula_c3.text_value() == R"(SUM(A1:B1)&"<ok>")",
            "post-recovery scalar/formula edits should read C3 formula text");
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientScalarFormula",
        "post-recovery scalar/formula mutations",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean scalar/formula recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the scalar/formula materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear scalar/formula dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear scalar/formula dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear scalar/formula dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after scalar/formula edits");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after scalar/formula edits");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "source package should still contain original A2 after scalar/formula edits");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "42.25",
        "source package should not contain later scalar edits");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "SUM(A1:B1)",
        "source package should not contain later formula edits");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first scalar/formula recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientScalarFormula",
        "first scalar/formula recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-scalar-formula-first",
        "first output should contain the saved value before scalar/formula edits");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "first output should still contain source-backed A2 before boolean replacement");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "42.25",
        "first output should not contain later number replacement");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "SUM(A1:B1)",
        "first output should not contain later formula replacement");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second scalar/formula recovery output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientScalarFormula",
        "second scalar/formula recovery output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "second scalar/formula output should refresh dimension through C3");
    check_contains(second_worksheet_xml, R"(<c r="A1"><v>42.25</v></c>)",
        "second output should persist numeric A1 replacement as scalar value");
    check_contains(second_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "second output should preserve source-backed B1 number");
    check_contains(second_worksheet_xml, R"(<c r="A2" t="b"><v>1</v></c>)",
        "second output should persist boolean A2 replacement");
    check_contains(second_worksheet_xml,
        R"(<c r="C3"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f></c>)",
        "second output should persist escaped formula C3 without cached value");
    check_not_contains(second_worksheet_xml, "rename-back-scalar-formula-first",
        "second output should remove prior text payload after number replacement");
    check_not_contains(second_worksheet_xml, "placeholder-a1",
        "second output should not reload stale source A1 after number replacement");
    check_not_contains(second_worksheet_xml, "placeholder-a2",
        "second output should replace source-backed A2 with a boolean cell");
}

void test_public_worksheet_editor_rename_back_failed_save_as_text_escape_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-text-escape-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-text-escape-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-text-escape-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientTextEscape");
    editor.rename_sheet("TransientTextEscape", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-text-escape-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before text escape recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before text escape edits should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired text escape handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };
    check(!editor.last_edit_error().has_value(),
        "text escape setup should start without edit diagnostics");
    {
        const fastxlsx::CellValue saved_a1 = sheet.get_cell(1, 1);
        check(saved_a1.kind() == fastxlsx::CellValueKind::Text &&
                saved_a1.text_value() == "rename-back-text-escape-first",
            "text escape setup should expose the saved materialized A1 value");
        const fastxlsx::CellValue preserved_a2 = reacquired.get_cell("A2");
        check(preserved_a2.kind() == fastxlsx::CellValueKind::Text &&
                preserved_a2.text_value() == "placeholder-a2",
            "text escape setup should preserve unchanged source-backed A2");
    }

    reacquired.set_cell("A1",
        fastxlsx::CellValue::text(R"(  A&B <C> "D"  )"));
    sheet.set_cell(2, 1, fastxlsx::CellValue::text(""));
    reacquired.set_cell(3, 3,
        fastxlsx::CellValue::text(R"(A&B <C> > "Q")"));

    check(!editor.last_edit_error().has_value(),
        "post-recovery text escape mutations should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "post-recovery text escape mutations should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "post-recovery text escape mutations should keep A1, B1, A2, and C3");
    {
        const fastxlsx::CellValue escaped_a1 = sheet.get_cell("A1");
        const fastxlsx::CellValue preserved_b1 = reacquired.get_cell("B1");
        const fastxlsx::CellValue empty_a2 = reacquired.get_cell(2, 1);
        const fastxlsx::CellValue escaped_c3 = sheet.get_cell("C3");
        check(escaped_a1.kind() == fastxlsx::CellValueKind::Text &&
                escaped_a1.text_value() == R"(  A&B <C> "D"  )",
            "post-recovery text escape edits should read back preserved whitespace text");
        check(preserved_b1.kind() == fastxlsx::CellValueKind::Number &&
                preserved_b1.number_value() == 1.0,
            "post-recovery text escape edits should preserve source-backed B1");
        check(empty_a2.kind() == fastxlsx::CellValueKind::Text &&
                empty_a2.text_value().empty(),
            "post-recovery text escape edits should read A2 as empty text");
        check(escaped_c3.kind() == fastxlsx::CellValueKind::Text &&
                escaped_c3.text_value() == R"(A&B <C> > "Q")",
            "post-recovery text escape edits should read C3 as special-character text");
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientTextEscape",
        "post-recovery text escape mutations",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean text escape recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the text escape materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear text escape dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear text escape dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear text escape dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after text escape edits");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after text escape edits");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "source package should still contain original A2 after text escape edits");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "A&amp;B",
        "source package should not contain later escaped text edits");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first text escape recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientTextEscape",
        "first text escape recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-text-escape-first",
        "first output should contain the saved value before text escape edits");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "first output should still contain source-backed A2 before empty replacement");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "A&amp;B",
        "first output should not contain later escaped text edits");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second text escape recovery output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientTextEscape",
        "second text escape recovery output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "second text escape output should refresh dimension through C3");
    check_contains(second_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t xml:space="preserve">  A&amp;B &lt;C&gt; "D"  </t></is></c>)",
        "second output should persist whitespace text with xml:space and escaped text");
    check_contains(second_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "second output should preserve source-backed B1 number");
    check_contains(second_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t></t></is></c>)",
        "second output should persist empty text as an empty t element");
    check_contains(second_worksheet_xml,
        R"(<c r="C3" t="inlineStr"><is><t>A&amp;B &lt;C&gt; &gt; "Q"</t></is></c>)",
        "second output should persist special-character text without xml:space");
    check_not_contains(second_worksheet_xml, "rename-back-text-escape-first",
        "second output should remove prior text payload after whitespace replacement");
    check_not_contains(second_worksheet_xml, "placeholder-a1",
        "second output should not reload stale source A1 after text replacement");
    check_not_contains(second_worksheet_xml, "placeholder-a2",
        "second output should replace source-backed A2 with empty text");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = workbook_editor_shard_from_args(argc, argv);
        std::printf("fastxlsx.workbook_editor shard: %.*s\n",
            static_cast<int>(shard.size()), shard.data());

        if (should_run_workbook_editor_shard(shard, "public-retry")) {
            test_public_worksheet_editor_rename_back_failed_mutation_preserves_clean_diagnostics();
            test_public_worksheet_editor_rename_back_failed_save_as_preserves_dirty_state();
            test_public_worksheet_editor_rename_back_failed_save_as_reacquire_reuses_saved_state();
            test_public_worksheet_editor_rename_back_failed_save_as_option_mismatch_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_missing_try_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_missing_worksheet_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_catalog_queries_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_diagnostics_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_handle_reads_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_invalid_reads_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_invalid_mutations_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_missing_erase_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_blank_and_existing_erase_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_scalar_and_formula_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_text_escape_projection();
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
