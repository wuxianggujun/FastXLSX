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
    return shard == "all" || shard == "core";
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
void test_replaces_sheet_data_and_preserves_untouched_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-replace-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string untouched_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    editor.replace_sheet_data("Data",
        {
            {fastxlsx::CellValue::number(42.25), fastxlsx::CellValue::text("fresh")},
            {fastxlsx::CellValue::formula("SUM(A1:A1)")},
        });
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(worksheet_xml, R"(<v>7</v>)",
        "second replacement should overwrite earlier queued data");
    check_contains(worksheet_xml, R"(<c r="A1"><v>42.25</v></c>)",
        "replaced sheet should carry new numeric cell");
    check_contains(worksheet_xml, R"(<c r="B1" t="inlineStr"><is><t>fresh</t></is></c>)",
        "replaced sheet should carry new inline text cell");
    check_contains(worksheet_xml, "<f>SUM(A1:A1)</f>",
        "replaced sheet should carry new formula cell");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "replaced sheet should drop old placeholder data");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "replaced sheet should drop old placeholder data");

    check(output_entries.at("xl/worksheets/sheet2.xml") == untouched_sheet_before,
        "untouched worksheet bytes should be preserved");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "content types bytes should be preserved");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "package relationships bytes should be preserved");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "workbook relationships bytes should be preserved");
}

void test_replace_sheet_data_preserves_image_sheet_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_image("fastxlsx-workbook-editor-image-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-image-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string picture_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string picture_sheet_rels_before =
        source_entries.at("xl/worksheets/_rels/sheet2.xml.rels");
    const std::string drawing_before = source_entries.at("xl/drawings/drawing1.xml");
    const std::string drawing_rels_before =
        source_entries.at("xl/drawings/_rels/drawing1.xml.rels");
    const std::string media_before = source_entries.at("xl/media/image1.png");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data",
        {
            {fastxlsx::CellValue::number(42.25), fastxlsx::CellValue::text("fresh")},
            {fastxlsx::CellValue::formula("SUM(A1:A1)")},
        });
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string data_sheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_sheet_xml, R"(<c r="A1"><v>42.25</v></c>)",
        "edited data sheet should carry new numeric cell");
    check_contains(data_sheet_xml, R"(<c r="B1" t="inlineStr"><is><t>fresh</t></is></c>)",
        "edited data sheet should carry new inline text cell");
    check_contains(data_sheet_xml, "<f>SUM(A1:A1)</f>",
        "edited data sheet should carry new formula cell");
    check_not_contains(data_sheet_xml, "placeholder-a1",
        "edited data sheet should drop old placeholder data");
    check_not_contains(data_sheet_xml, "placeholder-a2",
        "edited data sheet should drop old placeholder data");

    check(output_entries.at("xl/worksheets/sheet2.xml") == picture_sheet_before,
        "image worksheet bytes should be preserved");
    check(output_entries.at("xl/worksheets/_rels/sheet2.xml.rels") == picture_sheet_rels_before,
        "image worksheet relationships bytes should be preserved");
    check(output_entries.at("xl/drawings/drawing1.xml") == drawing_before,
        "image drawing XML should be preserved");
    check(output_entries.at("xl/drawings/_rels/drawing1.xml.rels") == drawing_rels_before,
        "image drawing relationships should be preserved");
    check(output_entries.at("xl/media/image1.png") == media_before,
        "image media bytes should be preserved");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "content types bytes should be preserved");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "package relationships bytes should be preserved");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "workbook relationships bytes should be preserved");
}

void test_replace_sheet_data_preserves_surrounding_worksheet_metadata()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-metadata-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-metadata-output.xlsx");

    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.set_column_width(1, 2, 18.75);
        data.freeze_panes(1, 1);
        data.set_auto_filter({1, 1, 3, 2});
        data.merge_cells({3, 1, 3, 2});
        data.append_row({fastxlsx::CellView::text("old-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("old-a2"),
            fastxlsx::CellView::number(2.0)});
        data.append_row({fastxlsx::CellView::text("old-a3"),
            fastxlsx::CellView::number(3.0)});
        writer.close();
    }

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(9.0), fastxlsx::CellValue::text("new-cell")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");

    check_contains(worksheet_xml, R"(<c r="A1"><v>9</v></c>)",
        "metadata-preserving replacement should write the new numeric cell");
    check_contains(worksheet_xml, R"(<c r="B1" t="inlineStr"><is><t>new-cell</t></is></c>)",
        "metadata-preserving replacement should write the new inline string cell");
    check_not_contains(worksheet_xml, "old-a1",
        "metadata-preserving replacement should remove old sheetData cells");
    check_not_contains(worksheet_xml, "old-a2",
        "metadata-preserving replacement should remove old sheetData cells");
    check_not_contains(worksheet_xml, "old-a3",
        "metadata-preserving replacement should remove old sheetData cells");

    check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "replace_sheet_data should preserve the source dimension metadata as-is");
    check_contains(worksheet_xml,
        R"(<col min="1" max="2" width="18.75" customWidth="1"/>)",
        "replace_sheet_data should preserve source column metadata");
    check_contains(worksheet_xml,
        R"(<pane xSplit="1" ySplit="1" topLeftCell="B2" activePane="bottomRight" state="frozen"/>)",
        "replace_sheet_data should preserve source frozen-pane metadata");
    check_contains(worksheet_xml, R"(<autoFilter ref="A1:B3"/>)",
        "replace_sheet_data should preserve source autoFilter metadata");
    check_contains(worksheet_xml,
        R"(<mergeCells count="1"><mergeCell ref="A3:B3"/></mergeCells>)",
        "replace_sheet_data should preserve source mergeCells metadata");
    check_contains(worksheet_xml, R"(</sheetData><autoFilter ref="A1:B3"/>)",
        "replace_sheet_data should replace only sheetData and keep suffix metadata after it");
}

void test_replace_sheet_data_writes_caller_style_ids_as_is()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-styled-replacement-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-styled-replacement-output.xlsx");

    fastxlsx::StyleId number_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        number_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("old styled").with_style(number_style),
            fastxlsx::CellView::text("old plain")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string styles_before = source_entries.at("xl/styles.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(9.5).with_style(number_style),
            fastxlsx::CellValue::text("explicit default").with_style(fastxlsx::StyleId {})}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == styles_before,
        "styled WorkbookEditor replacement should preserve source styles.xml bytes");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1" s="1"><v>9.5</v></c>)",
        "WorkbookEditor should write caller-supplied non-default StyleId as-is");
    check_contains(worksheet_xml, R"(<c r="B1" t="inlineStr"><is><t>explicit default</t></is></c>)",
        "WorkbookEditor should omit s=\"0\" for explicit default StyleId");
    check_not_contains(worksheet_xml, "old styled",
        "styled replacement should remove old sheetData text");
    check_not_contains(worksheet_xml, R"(r="B1" s="0")",
        "explicit default StyleId should not serialize a default style attribute");
}

void test_replace_sheet_data_distinguishes_blank_cells_from_missing_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-blank-replacement-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-blank-replacement-output.xlsx");

    fastxlsx::StyleId number_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        number_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("old-a"),
            fastxlsx::CellView::text("old-b"), fastxlsx::CellView::text("old-c")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string styles_before = source_entries.at("xl/styles.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::blank().with_style(number_style),
             fastxlsx::CellValue::text("after blank")},
            {},
            {fastxlsx::CellValue::blank(), fastxlsx::CellValue::number(42.0)}});

    check(editor.pending_replacement_cell_count() == 4,
        "blank replacement should count explicit blank cells but not empty rows");
    check(editor.has_pending_replacement("Data"),
        "blank replacement should still register a pending sheetData payload");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == styles_before,
        "blank replacement should preserve source styles.xml bytes");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" s="1"/><c r="B1" t="inlineStr"><is><t>after blank</t></is></c></row>)",
        "styled blank replacement should emit an empty styled cell before text");
    check_contains(worksheet_xml,
        R"(<row r="3"><c r="A3"/><c r="B3"><v>42</v></c></row>)",
        "default-style blank replacement should emit an empty cell and preserve row gaps");
    check_not_contains(worksheet_xml, R"(<row r="2")",
        "empty replacement rows should remain missing rows, not explicit blank rows");
    check_not_contains(worksheet_xml, "old-a",
        "blank replacement should remove old source sheetData text");
    check_not_contains(worksheet_xml, R"(s="0")",
        "blank replacement should omit explicit default style attributes");
}

void test_worksheet_names_and_has_worksheet()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-names-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    const std::vector<std::string> names = editor.worksheet_names();
    check(names.size() == 2, "worksheet_names should list both sheets");
    check(names.size() == 2 && names[0] == "Data",
        "worksheet_names should list sheets in catalog order");
    check(names.size() == 2 && names[1] == "Untouched",
        "worksheet_names should list the second sheet name");

    check(editor.has_worksheet("Data"), "has_worksheet should find an existing sheet");
    check(editor.has_worksheet("Untouched"), "has_worksheet should find the second sheet");
    check(!editor.has_worksheet("Missing"),
        "has_worksheet should reject an absent sheet name");

    const std::vector<std::string> source_names = editor.source_worksheet_names();
    check(source_names == names,
        "source_worksheet_names should match worksheet_names before queued edits");
    check(editor.has_source_worksheet("Data"),
        "has_source_worksheet should find an existing source sheet");
    check(editor.has_source_worksheet("Untouched"),
        "has_source_worksheet should find the second source sheet");
    check(!editor.has_source_worksheet("Missing"),
        "has_source_worksheet should reject an absent source sheet name");

    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
        editor.worksheet_catalog();
    check(catalog.size() == 2,
        "worksheet_catalog should list source-to-planned sheets");
    if (catalog.size() == 2) {
        check(catalog[0].source_name == "Data",
            "worksheet_catalog should preserve source sheet order");
        check(catalog[0].planned_name == "Data",
            "worksheet_catalog should match planned name before edits");
        check(!catalog[0].renamed,
            "worksheet_catalog should not mark unchanged source sheet as renamed");
        check(catalog[1].source_name == "Untouched",
            "worksheet_catalog should list the second source sheet");
        check(catalog[1].planned_name == "Untouched",
            "worksheet_catalog should list the second planned sheet");
        check(!catalog[1].renamed,
            "worksheet_catalog should not mark the second unchanged sheet as renamed");
    }
}

void test_moved_from_workbook_editor_operations_throw()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-moved-from-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-moved-from-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(moved.has_worksheet("Data"),
        "moved-to editor should keep the opened workbook state");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "moved-from worksheet_names should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { (void)editor.has_worksheet("Data"); }),
        "moved-from has_worksheet should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { (void)editor.source_worksheet_names(); }),
        "moved-from source_worksheet_names should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { (void)editor.has_source_worksheet("Data"); }),
        "moved-from has_source_worksheet should throw FastXlsxError");
    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});
    }), "moved-from replace_sheet_data should throw FastXlsxError");
    check(!editor.has_pending_changes(),
        "moved-from has_pending_changes should return false");
    check(editor.pending_change_count() == 0,
        "moved-from pending_change_count should return zero");
    check_workbook_editor_no_replacement_diagnostics(editor, "moved-from editor");
    check(!editor.has_pending_replacement("Data"),
        "moved-from pending replacement lookup should return false");
    check(editor.pending_worksheet_edits().empty(),
        "moved-from pending worksheet edit summaries should be empty");
    check(editor.worksheet_catalog().empty(),
        "moved-from worksheet_catalog should return an empty view");
    check(!editor.last_edit_error().has_value(),
        "moved-from last_edit_error should return no diagnostic");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Moved"); }),
        "moved-from rename_sheet should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { editor.save_as(output); }),
        "moved-from save_as should throw FastXlsxError");
}

void test_moved_to_workbook_editor_preserves_pending_public_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-moved-to-state-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-moved-to-state-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("moved-state")}});
    editor.rename_sheet("Data", "MovedReport");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Untouched", "Bad/Name"); }),
        "failed rename before move should record a public diagnostic");
    const std::optional<std::string> last_error_before_move = editor.last_edit_error();
    check(last_error_before_move.has_value(),
        "failed rename before move should set last_edit_error");

    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(moved.has_worksheet("MovedReport"),
        "moved-to editor should keep the planned renamed worksheet");
    check(!moved.has_worksheet("Data"),
        "moved-to editor should not expose the old planned worksheet name");
    check(moved.has_source_worksheet("Data"),
        "moved-to editor should keep the source workbook catalog");
    check(moved.has_pending_changes(),
        "moved-to editor should keep pending public edits");
    check(moved.pending_change_count() == 2,
        "moved-to editor should keep the successful public edit count");
    check(moved.pending_replacement_cell_count() == 1,
        "moved-to editor should keep pending replacement cell diagnostics");
    check(moved.estimated_pending_replacement_memory_usage() > 0,
        "moved-to editor should keep pending replacement memory diagnostics");
    {
        const std::vector<std::string> pending_names =
            moved.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "MovedReport",
            "moved-to editor should keep pending replacement planned names");
    }
    check(moved.has_pending_replacement("MovedReport"),
        "moved-to editor should keep pending replacement lookup by planned name");
    check(moved.last_edit_error() == last_error_before_move,
        "moved-to editor should keep the last failed public edit diagnostic");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            moved.pending_worksheet_edits();
        check(summaries.size() == 1,
            "moved-to editor should keep pending worksheet summaries");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data",
                "moved-to summary should keep the source sheet name");
            check(summaries[0].planned_name == "MovedReport",
                "moved-to summary should keep the planned sheet name");
            check(summaries[0].renamed,
                "moved-to summary should keep rename diagnostics");
            check(summaries[0].sheet_data_replaced,
                "moved-to summary should keep replacement diagnostics");
        }
    }

    moved.save_as(output);

    check(moved.last_edit_error() == last_error_before_move,
        "save_as on moved-to editor should not update last_edit_error");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="MovedReport")",
        "moved-to editor should save the queued rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "moved-state",
        "moved-to editor should save the queued replacement data");
}

void test_clean_moved_to_workbook_editor_preserves_noop_public_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-clean-moved-to-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-clean-moved-to-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(!editor.has_pending_changes(),
        "clean editor should start with no pending changes before move construction");
    check(!editor.last_edit_error().has_value(),
        "clean editor should start with no last_edit_error before move construction");

    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(moved.has_worksheet("Data"),
        "clean moved-to editor should keep the current planned catalog");
    check(moved.has_source_worksheet("Data"),
        "clean moved-to editor should keep the source workbook catalog");
    check(!moved.has_pending_changes(),
        "clean moved-to editor should keep no pending changes");
    check(moved.pending_change_count() == 0,
        "clean moved-to editor should keep zero pending public edits");
    check_workbook_editor_no_replacement_diagnostics(moved, "clean moved-to editor");
    check(!moved.has_pending_replacement("Data"),
        "clean moved-to editor should keep no pending replacement lookup");
    check(moved.pending_worksheet_edits().empty(),
        "clean moved-to editor should keep no pending worksheet edit summaries");
    check(!moved.last_edit_error().has_value(),
        "clean moved-to editor should keep no last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "clean moved-from editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "clean moved-from editor should report no pending changes");
    check(!editor.last_edit_error().has_value(),
        "clean moved-from editor should report no last_edit_error");

    moved.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "clean moved-to no-op save_as should copy the source package");
}

void test_moved_to_workbook_editor_preserves_replacement_options()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-moved-to-options-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-moved-to-options-output.xlsx");

    fastxlsx::WorkbookEditorOptions options;
    options.max_replacement_cells = 1;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source, options);
    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(threw_fastxlsx_error([&] {
        moved.replace_sheet_data("Data",
            {{fastxlsx::CellValue::text("too-many-a"),
                fastxlsx::CellValue::text("too-many-b")}});
    }), "moved-to editor should keep max_replacement_cells guardrail");
    check(!moved.has_pending_changes(),
        "failed guarded replacement after move should not queue public edits");
    check(moved.pending_replacement_cell_count() == 0,
        "failed guarded replacement after move should not add replacement cells");
    check(moved.last_edit_error().has_value(),
        "failed guarded replacement after move should set last_edit_error");

    moved.replace_sheet_data("Data", {{fastxlsx::CellValue::text("guarded-state")}});
    check(moved.has_pending_changes(),
        "valid guarded replacement after move should queue public edits");
    check(moved.pending_replacement_cell_count() == 1,
        "valid guarded replacement after move should record one replacement cell");
    check(!moved.last_edit_error().has_value(),
        "valid guarded replacement after move should clear last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "options moved-from editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "options moved-from editor should report no pending changes");

    moved.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "guarded-state",
        "moved-to editor with preserved options should save the valid replacement");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "too-many-a",
        "moved-to editor should not leak the rejected oversized replacement");
}

void test_moved_to_workbook_editor_preserves_replacement_memory_budget()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-moved-to-memory-budget-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-moved-to-memory-budget-output.xlsx");

    fastxlsx::WorkbookEditorOptions options;
    options.replacement_memory_budget_bytes = 1;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source, options);
    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(threw_fastxlsx_error([&] {
        moved.replace_sheet_data("Data", {{fastxlsx::CellValue::text("too large")}});
    }), "moved-to editor should keep replacement_memory_budget_bytes guardrail");
    check(!moved.has_pending_changes(),
        "moved-to memory-budget failure should not queue public edits");
    check_workbook_editor_no_replacement_payload_size_diagnostics(
        moved, "moved-to memory-budget failure");
    check(moved.last_edit_error().has_value(),
        "moved-to memory-budget failure should set last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "memory-budget moved-from editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "memory-budget moved-from editor should report no pending changes");

    moved.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "moved-to memory-budget no-op save_as should copy the source package");
}

void test_move_assigned_workbook_editor_replaces_target_with_pending_public_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-source.xlsx");
    const std::filesystem::path discarded_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-discarded-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("assigned-state")}});
    editor.rename_sheet("Data", "AssignedReport");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Untouched", "Bad/Name"); }),
        "failed rename before move assignment should record a public diagnostic");
    const std::optional<std::string> last_error_before_assignment = editor.last_edit_error();
    check(last_error_before_assignment.has_value(),
        "failed rename before move assignment should set last_edit_error");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(discarded_source);
    target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("discarded-state")}});
    target.rename_sheet("Data", "DiscardedReport");

    target = std::move(editor);

    check(target.has_worksheet("AssignedReport"),
        "move-assigned editor should keep the source planned renamed worksheet");
    check(!target.has_worksheet("DiscardedReport"),
        "move-assigned editor should discard the previous target planned state");
    check(target.has_source_worksheet("Data"),
        "move-assigned editor should keep the assigned source workbook catalog");
    check(target.pending_change_count() == 2,
        "move-assigned editor should keep the assigned successful edit count");
    {
        const std::vector<std::string> pending_names =
            target.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "AssignedReport",
            "move-assigned editor should keep assigned pending replacement names");
    }
    check(target.has_pending_replacement("AssignedReport"),
        "move-assigned editor should keep assigned pending replacement lookup");
    check(target.last_edit_error() == last_error_before_assignment,
        "move-assigned editor should keep assigned last failed public edit diagnostic");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "move-assigned-from editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "move-assigned-from editor should report no pending changes");
    check(!editor.last_edit_error().has_value(),
        "move-assigned-from editor should report no last edit error");

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="AssignedReport")",
        "move-assigned editor should save the assigned queued rename");
    check_not_contains(output_entries.at("xl/workbook.xml"), "DiscardedReport",
        "move-assigned output should not keep the discarded target rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "assigned-state",
        "move-assigned editor should save the assigned replacement data");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "discarded-state",
        "move-assigned output should not keep discarded target replacement data");
}

void test_move_assigned_clean_workbook_editor_clears_dirty_target_state()
{
    const std::filesystem::path clean_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-clean-source.xlsx");
    const std::filesystem::path dirty_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-dirty-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-clean-output.xlsx");

    fastxlsx::WorkbookEditor clean = fastxlsx::WorkbookEditor::open(clean_source);
    check(!clean.has_pending_changes(),
        "clean move-assignment source should start with no pending changes");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(dirty_source);
    target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("target-dirty-state")}});
    target.rename_sheet("Data", "DirtyTarget");
    check(threw_fastxlsx_error([&] { target.rename_sheet("Untouched", "Bad/Name"); }),
        "dirty target should record a failed public edit diagnostic before assignment");
    check(target.has_pending_changes(),
        "dirty target should have pending state before clean-source assignment");
    check(target.last_edit_error().has_value(),
        "dirty target should have last_edit_error before clean-source assignment");

    target = std::move(clean);

    check(target.has_worksheet("Data"),
        "clean-source move assignment should expose the assigned source catalog");
    check(!target.has_worksheet("DirtyTarget"),
        "clean-source move assignment should discard the old target planned name");
    check(!target.has_pending_changes(),
        "clean-source move assignment should clear old target pending changes");
    check(target.pending_change_count() == 0,
        "clean-source move assignment should clear old target pending count");
    check_workbook_editor_no_replacement_diagnostics(
        target, "clean-source move assignment");
    check(!target.has_pending_replacement("DirtyTarget"),
        "clean-source move assignment should clear old target replacement lookup");
    check(target.pending_worksheet_edits().empty(),
        "clean-source move assignment should clear old target edit summaries");
    check(!target.last_edit_error().has_value(),
        "clean-source move assignment should clear old target last_edit_error");

    check(threw_fastxlsx_error([&] { (void)clean.worksheet_names(); }),
        "clean move-assigned-from editor should throw for worksheet_names");
    check(!clean.has_pending_changes(),
        "clean move-assigned-from editor should report no pending changes");
    check(!clean.last_edit_error().has_value(),
        "clean move-assigned-from editor should report no last edit error");

    target.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(clean_source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "clean-source move-assigned no-op save_as should copy the assigned source package");
    check_not_contains(output_entries.at("xl/workbook.xml"), "DirtyTarget",
        "clean-source move-assigned output should not keep old target rename");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "target-dirty-state",
        "clean-source move-assigned output should not keep old target replacement data");
}

void test_move_assignment_revives_moved_from_target_workbook_editor()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-revive-source.xlsx");
    const std::filesystem::path target_seed =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-revive-target-seed.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-revive-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("revived-state")}});
    editor.rename_sheet("Data", "RevivedReport");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Untouched", "Bad/Name"); }),
        "source editor should record a failed public edit before revive assignment");
    const std::optional<std::string> last_error_before_assignment = editor.last_edit_error();
    check(last_error_before_assignment.has_value(),
        "source editor should have last_edit_error before revive assignment");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_seed);
    fastxlsx::WorkbookEditor holder = std::move(target);
    check(holder.has_worksheet("Data"),
        "holder should keep the target seed workbook after moving from target");
    check(threw_fastxlsx_error([&] { (void)target.worksheet_names(); }),
        "target should be moved-from before revive assignment");
    check(!target.has_pending_changes(),
        "moved-from target should report no pending changes before revive assignment");

    target = std::move(editor);

    check(target.has_worksheet("RevivedReport"),
        "move assignment should revive moved-from target with assigned planned catalog");
    check(target.has_source_worksheet("Data"),
        "revived target should expose the assigned source catalog");
    check(target.pending_change_count() == 2,
        "revived target should keep assigned pending public edit count");
    check(target.has_pending_replacement("RevivedReport"),
        "revived target should keep assigned pending replacement lookup");
    check(target.last_edit_error() == last_error_before_assignment,
        "revived target should keep assigned last failed public edit diagnostic");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "move-assigned-from source editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "move-assigned-from source editor should report no pending changes");
    check(!editor.last_edit_error().has_value(),
        "move-assigned-from source editor should report no last edit error");

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RevivedReport")",
        "revived target should save the assigned queued rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "revived-state",
        "revived target should save the assigned replacement data");
}

void test_move_assignment_replaces_target_replacement_options()
{
    const std::filesystem::path strict_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-options-source.xlsx");
    const std::filesystem::path permissive_target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-options-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-options-output.xlsx");

    fastxlsx::WorkbookEditorOptions strict_options;
    strict_options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor editor =
        fastxlsx::WorkbookEditor::open(strict_source, strict_options);

    fastxlsx::WorkbookEditorOptions permissive_options;
    permissive_options.max_replacement_cells = 10;
    fastxlsx::WorkbookEditor target =
        fastxlsx::WorkbookEditor::open(permissive_target_source, permissive_options);

    target = std::move(editor);

    check(threw_fastxlsx_error([&] {
        target.replace_sheet_data("Data",
            {{fastxlsx::CellValue::text("assigned-too-many-a"),
                fastxlsx::CellValue::text("assigned-too-many-b")}});
    }), "move-assigned editor should use assigned source replacement guardrails");
    check(!target.has_pending_changes(),
        "failed guarded replacement after assignment should not queue public edits");
    check(target.pending_replacement_cell_count() == 0,
        "failed guarded replacement after assignment should not add replacement cells");

    target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("assigned-guarded-state")}});
    check(target.has_pending_changes(),
        "valid guarded replacement after assignment should queue public edits");
    check(target.pending_replacement_cell_count() == 1,
        "valid guarded replacement after assignment should record one replacement cell");
    check(!target.last_edit_error().has_value(),
        "valid guarded replacement after assignment should clear last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "options move-assigned-from editor should throw for worksheet_names");

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "assigned-guarded-state",
        "move-assigned editor with source options should save the valid replacement");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "assigned-too-many-a",
        "move-assigned editor should not leak the rejected oversized replacement");
}

void test_move_assignment_replaces_target_replacement_memory_budget()
{
    const std::filesystem::path strict_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-memory-source.xlsx");
    const std::filesystem::path permissive_target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-memory-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-memory-output.xlsx");

    fastxlsx::WorkbookEditorOptions strict_options;
    strict_options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor editor =
        fastxlsx::WorkbookEditor::open(strict_source, strict_options);

    fastxlsx::WorkbookEditorOptions permissive_options;
    permissive_options.replacement_memory_budget_bytes = 1024 * 1024;
    fastxlsx::WorkbookEditor target =
        fastxlsx::WorkbookEditor::open(permissive_target_source, permissive_options);

    target = std::move(editor);

    check(threw_fastxlsx_error([&] {
        target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("assigned-too-large")}});
    }), "move-assigned editor should use assigned source memory-budget guardrail");
    check(!target.has_pending_changes(),
        "move-assigned memory-budget failure should not queue public edits");
    check_workbook_editor_no_replacement_payload_size_diagnostics(
        target, "move-assigned memory-budget failure");
    check(target.last_edit_error().has_value(),
        "move-assigned memory-budget failure should set last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "memory-budget move-assigned-from editor should throw for worksheet_names");

    target.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(strict_source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "move-assigned memory-budget no-op save_as should copy the assigned source package");
}

void test_move_assignment_clears_target_replacement_options_when_source_is_default()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-default-options-source.xlsx");
    const std::filesystem::path strict_target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-default-options-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-default-options-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    fastxlsx::WorkbookEditorOptions strict_options;
    strict_options.max_replacement_cells = 1;
    strict_options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor target =
        fastxlsx::WorkbookEditor::open(strict_target_source, strict_options);

    target = std::move(editor);

    target.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("default-options-a"),
            fastxlsx::CellValue::text("default-options-b")}});

    check(target.has_pending_changes(),
        "default-source move assignment should allow replacement after clearing target options");
    check(target.pending_replacement_cell_count() == 2,
        "default-source move assignment should not retain target max_replacement_cells");
    check(target.estimated_pending_replacement_memory_usage() > 1,
        "default-source move assignment should not retain target memory budget");
    check(!target.last_edit_error().has_value(),
        "valid replacement after default-source move assignment should keep no last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "default-options move-assigned-from editor should throw for worksheet_names");

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "default-options-a",
        "default-source move-assigned output should write the first replacement cell");
    check_contains(worksheet_xml, "default-options-b",
        "default-source move-assigned output should write the second replacement cell");
}

void test_move_assignment_from_moved_from_source_clears_dirty_target_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-moved-from-source.xlsx");
    const std::filesystem::path dirty_target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-moved-from-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-moved-from-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorkbookEditor holder = std::move(editor);
    check(holder.has_worksheet("Data"),
        "holder should keep the original source workbook after moving from editor");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "source editor should be moved-from before assignment");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(dirty_target_source);
    target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("discarded-dirty-state")}});
    target.rename_sheet("Data", "DirtyBeforeMovedFromAssign");
    check(threw_fastxlsx_error([&] { target.rename_sheet("Untouched", "Bad/Name"); }),
        "dirty target should record a failed public edit before moved-from assignment");
    check(target.has_pending_changes(),
        "dirty target should have pending changes before moved-from assignment");
    check(target.last_edit_error().has_value(),
        "dirty target should have a last edit diagnostic before moved-from assignment");

    target = std::move(editor);

    check(threw_fastxlsx_error([&] { (void)target.worksheet_names(); }),
        "assignment from a moved-from source should leave the target moved-from");
    check(!target.has_pending_changes(),
        "assignment from a moved-from source should clear target pending changes");
    check(target.pending_change_count() == 0,
        "assignment from a moved-from source should clear target pending count");
    check_workbook_editor_no_replacement_diagnostics(
        target, "assignment from a moved-from source");
    check(target.pending_materialized_worksheet_names().empty(),
        "assignment from a moved-from source should clear dirty materialized names");
    check(!target.has_pending_replacement("DirtyBeforeMovedFromAssign"),
        "assignment from a moved-from source should clear target replacement lookup");
    check(target.pending_worksheet_edits().empty(),
        "assignment from a moved-from source should clear target edit summaries");
    check(target.worksheet_catalog().empty(),
        "assignment from a moved-from source should expose an empty moved-from catalog");
    check(!target.last_edit_error().has_value(),
        "assignment from a moved-from source should clear target last_edit_error");
    check(threw_fastxlsx_error([&] { target.save_as(output); }),
        "assignment from a moved-from source should make save_as throw");
    check(holder.has_worksheet("Data"),
        "assigning from the moved-from source should not disturb the prior moved-to holder");
}

void test_internal_materialized_sessions_move_with_workbook_editor_impl()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-move-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-move-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-move-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 1,
        "test hook should insert one materialized session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "newly materialized source session should start clean");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 2, 2, fastxlsx::CellValue::text("dirty-materialized"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "test hook mutation should mark the materialized session dirty");
    editor.replace_sheet_data(
        "Untouched", {{fastxlsx::CellValue::text("queued-other-sheet-before-move")}});
    check(editor.pending_change_count() == 1,
        "queued cross-sheet public edit should coexist with a dirty materialized session");
    check(editor.pending_replacement_cell_count() == 1,
        "queued cross-sheet public edit should record one replacement cell before move");
    {
        const std::vector<std::string> dirty_names =
            fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_names(editor);
        check(dirty_names.size() == 1 && dirty_names[0] == "Data",
            "dirty materialized session names should expose the planned sheet name");
    }

    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "moved-from editor should expose no materialized sessions");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(moved) == 1,
        "move construction should transfer materialized sessions with Impl state");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(moved) == 1,
        "move construction should preserve materialized dirty state");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(moved, "Data"),
        "move construction should keep the materialized planned sheet name");
    check(moved.pending_change_count() == 1,
        "move construction should preserve queued cross-sheet public edits");
    check(moved.pending_replacement_cell_count() == 1,
        "move construction should preserve queued replacement diagnostics");
    {
        const std::vector<std::string> names = moved.pending_replacement_worksheet_names();
        check(names.size() == 1 && names[0] == "Untouched",
            "move construction should preserve queued replacement planned names");
    }

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        target, "Untouched", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Untouched", 1, 1, fastxlsx::CellValue::text("discarded-target-materialized"));
    target.replace_sheet_data(
        "Data", {{fastxlsx::CellValue::text("discarded-target-public-replacement")}});
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(target, "Untouched"),
        "target should start with its own materialized session before assignment");

    target = std::move(moved);

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(moved) == 0,
        "move-assigned-from editor should expose no materialized sessions");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(target) == 1,
        "move assignment should replace target materialized sessions");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 1,
        "move assignment should preserve assigned materialized dirty state");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(target, "Data"),
        "move assignment should keep assigned materialized session");
    check(!fastxlsx::detail::testing_workbook_editor_has_materialized_session(target, "Untouched"),
        "move assignment should discard previous target materialized session");
    check(target.pending_change_count() == 1,
        "move assignment should preserve queued source public edits");
    check(target.pending_replacement_cell_count() == 1,
        "move assignment should discard target replacement diagnostics");
    {
        const std::vector<std::string> names = target.pending_replacement_worksheet_names();
        check(names.size() == 1 && names[0] == "Untouched",
            "move assignment should preserve only source queued replacement names");
    }

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "flush after move assignment should clear the assigned dirty materialized state");
    check(target.pending_change_count() == 2,
        "flush after move assignment should queue a materialized projection beside public edits");

    target.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_xml = output_entries.at("xl/worksheets/sheet2.xml");
    check_contains(data_xml, "dirty-materialized",
        "move-assigned materialized session should flush and save its payload");
    check_contains(untouched_xml, "queued-other-sheet-before-move",
        "move-assigned editor should save the queued source public replacement");
    check_not_contains(data_xml, "discarded-target-public-replacement",
        "move assignment should not leak discarded target public replacement");
    check_not_contains(untouched_xml, "discarded-target-materialized",
        "move assignment should not leak discarded target materialized session");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = workbook_editor_shard_from_args(argc, argv);
        std::printf("fastxlsx.workbook_editor shard: %.*s\n",
            static_cast<int>(shard.size()), shard.data());

        if (should_run_workbook_editor_shard(shard, "core")) {
            test_replaces_sheet_data_and_preserves_untouched_parts();
            test_replace_sheet_data_preserves_image_sheet_parts();
            test_replace_sheet_data_preserves_surrounding_worksheet_metadata();
            test_replace_sheet_data_writes_caller_style_ids_as_is();
            test_replace_sheet_data_distinguishes_blank_cells_from_missing_cells();
            test_worksheet_names_and_has_worksheet();
            test_moved_from_workbook_editor_operations_throw();
            test_moved_to_workbook_editor_preserves_pending_public_state();
            test_clean_moved_to_workbook_editor_preserves_noop_public_state();
            test_moved_to_workbook_editor_preserves_replacement_options();
            test_moved_to_workbook_editor_preserves_replacement_memory_budget();
            test_move_assigned_workbook_editor_replaces_target_with_pending_public_state();
            test_move_assigned_clean_workbook_editor_clears_dirty_target_state();
            test_move_assignment_revives_moved_from_target_workbook_editor();
            test_move_assignment_replaces_target_replacement_options();
            test_move_assignment_replaces_target_replacement_memory_budget();
            test_move_assignment_clears_target_replacement_options_when_source_is_default();
            test_move_assignment_from_moved_from_source_clears_dirty_target_state();
            test_internal_materialized_sessions_move_with_workbook_editor_impl();
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
