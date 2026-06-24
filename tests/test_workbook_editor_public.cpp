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
    return shard == "all" || shard == "public";
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
void test_public_worksheet_editor_handles_invalidate_after_owner_move()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-move-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-move-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor old_handle = editor.worksheet("Data");
    old_handle.set_cell(1, 1, fastxlsx::CellValue::text("moved-handle-before"));

    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(threw_fastxlsx_error([&] { (void)old_handle.has_pending_changes(); }),
        "WorksheetEditor handle borrowed before owner move construction should be invalid");
    check(threw_fastxlsx_error([&] {
        old_handle.set_cell(1, 1, fastxlsx::CellValue::text("stale-handle-write"));
    }), "invalidated WorksheetEditor handle should reject writes after owner move");

    fastxlsx::WorksheetEditor reacquired = moved.worksheet("Data");
    check(reacquired.has_pending_changes(),
        "reacquired handle should see the moved dirty materialized session");
    const fastxlsx::CellValue moved_value = reacquired.get_cell(1, 1);
    check(moved_value.kind() == fastxlsx::CellValueKind::Text &&
            moved_value.text_value() == "moved-handle-before",
        "reacquired handle should read the moved materialized cell state");
    reacquired.set_cell(2, 1, fastxlsx::CellValue::text("moved-handle-after"));

    moved.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "moved-handle-before",
        "moved-to editor should save state written before owner move");
    check_contains(worksheet_xml, "moved-handle-after",
        "moved-to editor should save state written through reacquired handle");
    check_not_contains(worksheet_xml, "stale-handle-write",
        "invalidated pre-move handle should not mutate moved-to state");
}

void test_public_worksheet_editor_invalidated_handle_failures_preserve_owner_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-invalidated-handle-diagnostics-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-invalidated-handle-diagnostics-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor old_handle = editor.worksheet("Data");
    old_handle.set_cell(1, 1, fastxlsx::CellValue::text("invalidated-handle-before"));

    check(threw_fastxlsx_error([&] {
        old_handle.set_cell("a1", fastxlsx::CellValue::text("invalidated-handle-sentinel"));
    }), "invalid mutation should seed last_edit_error before owner move");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "invalid mutation should leave a diagnostic before owner move");
    if (prior_error.has_value()) {
        check_contains(*prior_error, "WorksheetEditor cell reference is invalid",
            "seed diagnostic should come from the invalid A1 mutation");
    }

    const std::vector<std::string> expected_dirty_names =
        editor.pending_materialized_worksheet_names();
    const std::size_t expected_dirty_cells = editor.pending_materialized_cell_count();
    const std::size_t expected_dirty_memory =
        editor.estimated_pending_materialized_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        editor.pending_worksheet_edits();

    fastxlsx::WorkbookEditor moved = std::move(editor);
    check(moved.last_edit_error() == prior_error,
        "owner move should transfer the prior public diagnostic");
    check(!editor.last_edit_error().has_value(),
        "moved-from editor should expose no public diagnostic");

    const auto check_moved_state = [&] (std::string_view prefix) {
        check(moved.last_edit_error() == prior_error,
            std::string(prefix) + " should preserve moved-to last_edit_error");
        check(moved.pending_change_count() == 0,
            std::string(prefix) + " should not queue a Patch handoff");
        check(moved.pending_materialized_worksheet_names() == expected_dirty_names,
            std::string(prefix) + " should preserve dirty materialized names");
        check(moved.pending_materialized_cell_count() == expected_dirty_cells,
            std::string(prefix) + " should preserve dirty materialized cell count");
        check(moved.estimated_pending_materialized_memory_usage() == expected_dirty_memory,
            std::string(prefix) + " should preserve dirty materialized memory");
        check(workbook_editor_edit_summaries_equal(
                  moved.pending_worksheet_edits(), expected_summaries),
            std::string(prefix) + " should preserve materialized edit summaries");

        fastxlsx::WorksheetEditor reacquired = moved.worksheet("Data");
        check(reacquired.has_pending_changes(),
            std::string(prefix) + " should preserve reacquired dirty state");
        const fastxlsx::CellValue moved_value = reacquired.get_cell(1, 1);
        check(moved_value.kind() == fastxlsx::CellValueKind::Text &&
                moved_value.text_value() == "invalidated-handle-before",
            std::string(prefix) + " should preserve the pre-move materialized cell");
    };

    check(threw_fastxlsx_error([&] { (void)old_handle.has_pending_changes(); }),
        "invalidated handle should reject dirty-state reads after owner move");
    check_moved_state("invalidated has_pending_changes");
    check(threw_fastxlsx_error([&] { (void)old_handle.cell_count(); }),
        "invalidated handle should reject cell-count reads after owner move");
    check_moved_state("invalidated cell_count");
    check(threw_fastxlsx_error([&] { (void)old_handle.try_cell(1, 1); }),
        "invalidated handle should reject cell reads after owner move");
    check_moved_state("invalidated try_cell");
    check(threw_fastxlsx_error([&] { (void)old_handle.get_cell("A1"); }),
        "invalidated handle should reject throwing A1 reads after owner move");
    check_moved_state("invalidated get_cell");
    check(threw_fastxlsx_error([&] { (void)old_handle.sparse_cells(); }),
        "invalidated handle should reject sparse snapshots after owner move");
    check_moved_state("invalidated sparse_cells");
    check(threw_fastxlsx_error([&] {
        (void)old_handle.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    }), "invalidated handle should reject ranged sparse snapshots after owner move");
    check_moved_state("invalidated ranged sparse_cells");
    check(threw_fastxlsx_error([&] { (void)old_handle.estimated_memory_usage(); }),
        "invalidated handle should reject memory estimate reads after owner move");
    check_moved_state("invalidated estimated_memory_usage");
    check(threw_fastxlsx_error([&] {
        old_handle.set_cell(2, 1, fastxlsx::CellValue::text("stale-invalidated-write"));
    }), "invalidated handle should reject stale writes after owner move");
    check_moved_state("invalidated set_cell");
    check(threw_fastxlsx_error([&] { old_handle.erase_cell(1, 1); }),
        "invalidated handle should reject stale erases after owner move");
    check_moved_state("invalidated erase_cell");

    moved.save_as(output);
    check(moved.last_edit_error() == prior_error,
        "save_as after invalidated-handle failures should preserve prior diagnostic");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "invalidated-handle-before",
        "moved-to output should keep the pre-move materialized value");
    check_not_contains(worksheet_xml, "stale-invalidated-write",
        "invalidated handle should not write stale data into moved-to output");
    check_not_contains(worksheet_xml, "invalidated-handle-sentinel",
        "failed diagnostic seed should not write invalid-reference data");
}

void test_public_worksheet_editor_handles_invalidate_after_move_assignment()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-assign-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-assign-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-assign-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor source_handle = editor.worksheet("Data");
    source_handle.set_cell(1, 1, fastxlsx::CellValue::text("assigned-source-before"));

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::WorksheetEditor target_handle = target.worksheet("Data");
    target_handle.set_cell(1, 1, fastxlsx::CellValue::text("discarded-target-before"));

    target = std::move(editor);

    check(threw_fastxlsx_error([&] { (void)source_handle.has_pending_changes(); }),
        "source WorksheetEditor handle should be invalid after move assignment");
    check(threw_fastxlsx_error([&] { (void)target_handle.has_pending_changes(); }),
        "overwritten target WorksheetEditor handle should be invalid after move assignment");
    check(threw_fastxlsx_error([&] {
        target_handle.set_cell(1, 1, fastxlsx::CellValue::text("stale-target-write"));
    }), "invalidated target handle should not attach to the assigned source session");

    fastxlsx::WorksheetEditor reacquired = target.worksheet("Data");
    check(reacquired.has_pending_changes(),
        "reacquired assigned handle should see the assigned dirty source session");
    const fastxlsx::CellValue assigned_value = reacquired.get_cell(1, 1);
    check(assigned_value.kind() == fastxlsx::CellValueKind::Text &&
            assigned_value.text_value() == "assigned-source-before",
        "reacquired assigned handle should read the source materialized state");
    reacquired.set_cell(2, 1, fastxlsx::CellValue::text("assigned-source-after"));

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "assigned-source-before",
        "move-assigned editor should save source handle edits made before assignment");
    check_contains(worksheet_xml, "assigned-source-after",
        "move-assigned editor should save edits made through a reacquired handle");
    check_not_contains(worksheet_xml, "discarded-target-before",
        "move assignment should discard overwritten target materialized edits");
    check_not_contains(worksheet_xml, "stale-target-write",
        "invalidated target handle should not mutate the assigned source state");
}

void test_public_worksheet_editor_move_assignment_invalidated_handle_failures_preserve_owner_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-assign-invalidated-diagnostics-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-assign-invalidated-diagnostics-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-assign-invalidated-diagnostics-output.xlsx");

    fastxlsx::WorkbookEditor source_editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor source_handle = source_editor.worksheet("Data");
    source_handle.set_cell(1, 1,
        fastxlsx::CellValue::text("assigned-invalidated-source-before"));
    check(threw_fastxlsx_error([&] {
        source_handle.set_cell("a1",
            fastxlsx::CellValue::text("assigned-invalidated-source-sentinel"));
    }), "source invalid mutation should seed last_edit_error before move assignment");
    const std::optional<std::string> source_prior_error =
        source_editor.last_edit_error();
    check(source_prior_error.has_value(),
        "source editor should have a prior diagnostic before move assignment");

    const std::vector<std::string> expected_dirty_names =
        source_editor.pending_materialized_worksheet_names();
    const std::size_t expected_dirty_cells =
        source_editor.pending_materialized_cell_count();
    const std::size_t expected_dirty_memory =
        source_editor.estimated_pending_materialized_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        source_editor.pending_worksheet_edits();

    fastxlsx::WorkbookEditor target_editor = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::WorksheetEditor target_handle = target_editor.worksheet("Data");
    target_handle.set_cell(1, 1,
        fastxlsx::CellValue::text("discarded-target-invalidated-before"));
    check(threw_fastxlsx_error([&] {
        target_handle.set_cell(0, 1,
            fastxlsx::CellValue::text("discarded-target-invalidated-sentinel"));
    }), "target invalid mutation should seed a diagnostic before it is overwritten");
    const std::optional<std::string> discarded_target_error =
        target_editor.last_edit_error();
    check(discarded_target_error.has_value() &&
            discarded_target_error != source_prior_error,
        "target pre-assignment diagnostic should be independent from the source");

    target_editor = std::move(source_editor);
    check(target_editor.last_edit_error() == source_prior_error,
        "move assignment should keep the assigned source diagnostic");
    check(!source_editor.last_edit_error().has_value(),
        "move-assigned-from editor should expose no public diagnostic");

    const auto check_assigned_state = [&] (std::string_view prefix) {
        check(target_editor.last_edit_error() == source_prior_error,
            std::string(prefix) + " should preserve assigned last_edit_error");
        check(target_editor.pending_change_count() == 0,
            std::string(prefix) + " should not queue a Patch handoff");
        check(target_editor.pending_materialized_worksheet_names() == expected_dirty_names,
            std::string(prefix) + " should preserve assigned dirty names");
        check(target_editor.pending_materialized_cell_count() == expected_dirty_cells,
            std::string(prefix) + " should preserve assigned dirty cell count");
        check(target_editor.estimated_pending_materialized_memory_usage() ==
                expected_dirty_memory,
            std::string(prefix) + " should preserve assigned dirty memory");
        check(workbook_editor_edit_summaries_equal(
                  target_editor.pending_worksheet_edits(), expected_summaries),
            std::string(prefix) + " should preserve assigned edit summaries");

        fastxlsx::WorksheetEditor reacquired = target_editor.worksheet("Data");
        check(reacquired.has_pending_changes(),
            std::string(prefix) + " should preserve assigned dirty state");
        const fastxlsx::CellValue assigned_value = reacquired.get_cell(1, 1);
        check(assigned_value.kind() == fastxlsx::CellValueKind::Text &&
                assigned_value.text_value() == "assigned-invalidated-source-before",
            std::string(prefix) + " should preserve assigned source cell value");
    };

    check(threw_fastxlsx_error([&] { (void)source_handle.has_pending_changes(); }),
        "source handle should be invalid after move assignment");
    check_assigned_state("invalidated source has_pending_changes");
    check(threw_fastxlsx_error([&] { (void)source_handle.get_cell("A1"); }),
        "source handle should reject reads after move assignment");
    check_assigned_state("invalidated source get_cell");
    check(threw_fastxlsx_error([&] {
        (void)source_handle.estimated_memory_usage();
    }), "source handle should reject memory estimate reads after move assignment");
    check_assigned_state("invalidated source estimated_memory_usage");
    check(threw_fastxlsx_error([&] {
        source_handle.set_cell(2, 1,
            fastxlsx::CellValue::text("stale-source-assignment-write"));
    }), "invalidated source handle should reject stale writes");
    check_assigned_state("invalidated source set_cell");
    check(threw_fastxlsx_error([&] { (void)target_handle.has_pending_changes(); }),
        "overwritten target handle should be invalid after move assignment");
    check_assigned_state("invalidated target has_pending_changes");
    check(threw_fastxlsx_error([&] {
        (void)target_handle.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    }), "overwritten target handle should reject ranged sparse reads after move assignment");
    check_assigned_state("invalidated target ranged sparse_cells");
    check(threw_fastxlsx_error([&] { (void)target_handle.estimated_memory_usage(); }),
        "overwritten target handle should reject memory estimate reads after move assignment");
    check_assigned_state("invalidated target estimated_memory_usage");
    check(threw_fastxlsx_error([&] {
        target_handle.set_cell(2, 1,
            fastxlsx::CellValue::text("stale-target-assignment-write"));
    }), "invalidated target handle should reject stale writes");
    check_assigned_state("invalidated target set_cell");
    check(threw_fastxlsx_error([&] { target_handle.erase_cell(1, 1); }),
        "invalidated target handle should reject stale erases");
    check_assigned_state("invalidated target erase_cell");

    target_editor.save_as(output);
    check(target_editor.last_edit_error() == source_prior_error,
        "save_as after move-assignment invalidated-handle failures should preserve diagnostic");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "assigned-invalidated-source-before",
        "move-assigned output should keep the assigned source value");
    check_not_contains(worksheet_xml, "discarded-target-invalidated-before",
        "move-assigned output should not keep overwritten target value");
    check_not_contains(worksheet_xml, "stale-source-assignment-write",
        "invalidated source handle should not write stale data");
    check_not_contains(worksheet_xml, "stale-target-assignment-write",
        "invalidated target handle should not write stale data");
    check_not_contains(worksheet_xml, "assigned-invalidated-source-sentinel",
        "failed source diagnostic seed should not write invalid-reference data");
    check_not_contains(worksheet_xml, "discarded-target-invalidated-sentinel",
        "failed target diagnostic seed should not write discarded invalid-reference data");
}

void test_public_worksheet_editor_saved_clean_handle_invalidated_after_owner_move_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-move-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-move-first.xlsx");
    const std::filesystem::path moved_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-move-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor old_handle = editor.worksheet("Data");
    old_handle.set_cell(1, 1,
        fastxlsx::CellValue::text("saved-clean-move-before"));
    editor.save_as(first_output);

    check(!old_handle.has_pending_changes(),
        "owner-move setup should leave the saved materialized handle clean");
    check(editor.pending_change_count() == 1,
        "owner-move setup should retain the saved materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "owner-move setup should have no dirty materialized names after save");

    check(threw_fastxlsx_error([&] {
        old_handle.set_cell(0, 1,
            fastxlsx::CellValue::text("saved-clean-move-diagnostic-sentinel"));
    }), "invalid mutation after save should seed a diagnostic without dirtying the saved session");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "owner-move saved-clean setup should record a diagnostic before move");
    check(!old_handle.has_pending_changes(),
        "invalid mutation should keep the saved materialized handle clean before move");

    const std::size_t expected_pending_count = editor.pending_change_count();
    const std::vector<std::string> expected_dirty_names =
        editor.pending_materialized_worksheet_names();
    const std::size_t expected_dirty_cells = editor.pending_materialized_cell_count();
    const std::size_t expected_dirty_memory =
        editor.estimated_pending_materialized_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        editor.pending_worksheet_edits();
    check(expected_dirty_names.empty() && expected_dirty_cells == 0 &&
            expected_dirty_memory == 0,
        "owner-move saved-clean setup should keep materialized diagnostics clean");

    fastxlsx::WorkbookEditor moved = std::move(editor);
    check(moved.last_edit_error() == prior_error,
        "owner move should transfer the saved-clean diagnostic");
    check(!editor.last_edit_error().has_value(),
        "moved-from saved-clean editor should expose no public diagnostic");

    const auto check_moved_clean_state = [&] (std::string_view prefix) {
        check(moved.last_edit_error() == prior_error,
            std::string(prefix) + " should preserve moved-to last_edit_error");
        check(moved.pending_change_count() == expected_pending_count,
            std::string(prefix) + " should preserve the saved materialized handoff count");
        check(moved.pending_materialized_worksheet_names() == expected_dirty_names,
            std::string(prefix) + " should keep dirty materialized names empty");
        check(moved.pending_materialized_cell_count() == expected_dirty_cells,
            std::string(prefix) + " should keep dirty materialized cell count clear");
        check(moved.estimated_pending_materialized_memory_usage() ==
                expected_dirty_memory,
            std::string(prefix) + " should keep dirty materialized memory clear");
        check(workbook_editor_edit_summaries_equal(
                  moved.pending_worksheet_edits(), expected_summaries),
            std::string(prefix) + " should preserve saved-clean edit summaries");

        fastxlsx::WorksheetEditor reacquired = moved.worksheet("Data");
        check(!reacquired.has_pending_changes(),
            std::string(prefix) + " should preserve the clean saved session");
        const fastxlsx::CellValue moved_value = reacquired.get_cell(1, 1);
        check(moved_value.kind() == fastxlsx::CellValueKind::Text &&
                moved_value.text_value() == "saved-clean-move-before",
            std::string(prefix) + " should preserve the saved materialized value");
    };

    check(threw_fastxlsx_error([&] { (void)old_handle.has_pending_changes(); }),
        "saved-clean handle should be invalid after owner move");
    check_moved_clean_state("saved-clean invalidated has_pending_changes");
    check(threw_fastxlsx_error([&] { (void)old_handle.get_cell("A1"); }),
        "saved-clean handle should reject reads after owner move");
    check_moved_clean_state("saved-clean invalidated get_cell");
    check(threw_fastxlsx_error([&] { (void)old_handle.estimated_memory_usage(); }),
        "saved-clean handle should reject memory reads after owner move");
    check_moved_clean_state("saved-clean invalidated estimated_memory_usage");
    check(threw_fastxlsx_error([&] {
        old_handle.set_cell(2, 1,
            fastxlsx::CellValue::text("stale-saved-clean-move-write"));
    }), "saved-clean handle should reject stale writes after owner move");
    check_moved_clean_state("saved-clean invalidated set_cell");
    check(threw_fastxlsx_error([&] { old_handle.erase_cell(1, 1); }),
        "saved-clean handle should reject stale erases after owner move");
    check_moved_clean_state("saved-clean invalidated erase_cell");

    moved.save_as(moved_output);
    check(moved.last_edit_error() == prior_error,
        "save_as after saved-clean invalidated-handle failures should preserve diagnostic");

    const auto output_entries = fastxlsx::test::read_zip_entries(moved_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "saved-clean-move-before",
        "moved-to output should keep the saved materialized value");
    check_not_contains(worksheet_xml, "stale-saved-clean-move-write",
        "invalidated saved-clean handle should not write stale data into output");
    check_not_contains(worksheet_xml, "saved-clean-move-diagnostic-sentinel",
        "failed diagnostic seed should not write invalid-coordinate data");
}

void test_public_worksheet_editor_saved_clean_handles_invalidated_after_move_assignment_preserve_source_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-assign-source.xlsx");
    const std::filesystem::path source_first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-assign-source-first.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-assign-target.xlsx");
    const std::filesystem::path target_first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-assign-target-first.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-assign-output.xlsx");

    fastxlsx::WorkbookEditor source_editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor source_handle = source_editor.worksheet("Data");
    source_handle.set_cell(1, 1,
        fastxlsx::CellValue::text("saved-clean-assigned-source-before"));
    source_editor.save_as(source_first_output);
    check(!source_handle.has_pending_changes(),
        "move-assignment source setup should leave the saved handle clean");

    check(threw_fastxlsx_error([&] {
        source_handle.set_cell(0, 1,
            fastxlsx::CellValue::text("saved-clean-assigned-source-sentinel"));
    }), "source invalid mutation after save should seed a diagnostic");
    const std::optional<std::string> source_prior_error =
        source_editor.last_edit_error();
    check(source_prior_error.has_value(),
        "move-assignment source should have a diagnostic before assignment");
    check(!source_handle.has_pending_changes(),
        "source diagnostic failure should keep the saved source handle clean");

    const std::size_t expected_pending_count = source_editor.pending_change_count();
    const std::vector<std::string> expected_dirty_names =
        source_editor.pending_materialized_worksheet_names();
    const std::size_t expected_dirty_cells =
        source_editor.pending_materialized_cell_count();
    const std::size_t expected_dirty_memory =
        source_editor.estimated_pending_materialized_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        source_editor.pending_worksheet_edits();
    check(expected_dirty_names.empty() && expected_dirty_cells == 0 &&
            expected_dirty_memory == 0,
        "move-assignment source setup should keep materialized diagnostics clean");

    fastxlsx::WorkbookEditor target_editor = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::WorksheetEditor target_handle = target_editor.worksheet("Data");
    target_handle.set_cell(1, 1,
        fastxlsx::CellValue::text("discarded-saved-clean-target-before"));
    target_editor.save_as(target_first_output);
    check(!target_handle.has_pending_changes(),
        "move-assignment target setup should leave the overwritten handle clean");
    check(threw_fastxlsx_error([&] {
        target_handle.set_cell("a1",
            fastxlsx::CellValue::text("discarded-saved-clean-target-sentinel"));
    }), "target invalid mutation after save should seed an independent diagnostic");
    check(target_editor.last_edit_error().has_value(),
        "overwritten target should have a diagnostic before assignment");
    check(!target_handle.has_pending_changes(),
        "target diagnostic failure should keep the overwritten saved handle clean");

    target_editor = std::move(source_editor);
    check(target_editor.last_edit_error() == source_prior_error,
        "saved-clean move assignment should keep the assigned source diagnostic");
    check(!source_editor.last_edit_error().has_value(),
        "saved-clean move-assigned-from editor should expose no diagnostic");

    const auto check_assigned_clean_state = [&] (std::string_view prefix) {
        check(target_editor.last_edit_error() == source_prior_error,
            std::string(prefix) + " should preserve assigned source last_edit_error");
        check(target_editor.pending_change_count() == expected_pending_count,
            std::string(prefix) + " should preserve assigned materialized handoff count");
        check(target_editor.pending_materialized_worksheet_names() == expected_dirty_names,
            std::string(prefix) + " should keep assigned dirty materialized names empty");
        check(target_editor.pending_materialized_cell_count() == expected_dirty_cells,
            std::string(prefix) + " should keep assigned dirty cell count clear");
        check(target_editor.estimated_pending_materialized_memory_usage() ==
                expected_dirty_memory,
            std::string(prefix) + " should keep assigned dirty memory clear");
        check(workbook_editor_edit_summaries_equal(
                  target_editor.pending_worksheet_edits(), expected_summaries),
            std::string(prefix) + " should preserve assigned saved-clean summaries");

        fastxlsx::WorksheetEditor reacquired = target_editor.worksheet("Data");
        check(!reacquired.has_pending_changes(),
            std::string(prefix) + " should keep the assigned saved session clean");
        const fastxlsx::CellValue assigned_value = reacquired.get_cell(1, 1);
        check(assigned_value.kind() == fastxlsx::CellValueKind::Text &&
                assigned_value.text_value() == "saved-clean-assigned-source-before",
            std::string(prefix) + " should preserve the assigned saved source value");
    };

    check(threw_fastxlsx_error([&] { (void)source_handle.has_pending_changes(); }),
        "saved-clean source handle should be invalid after move assignment");
    check_assigned_clean_state("saved-clean source invalidated has_pending_changes");
    check(threw_fastxlsx_error([&] { (void)source_handle.get_cell("A1"); }),
        "saved-clean source handle should reject reads after move assignment");
    check_assigned_clean_state("saved-clean source invalidated get_cell");
    check(threw_fastxlsx_error([&] { (void)source_handle.estimated_memory_usage(); }),
        "saved-clean source handle should reject memory reads after move assignment");
    check_assigned_clean_state("saved-clean source invalidated estimated_memory_usage");
    check(threw_fastxlsx_error([&] {
        source_handle.set_cell(2, 1,
            fastxlsx::CellValue::text("stale-saved-clean-source-assignment-write"));
    }), "saved-clean source handle should reject stale writes after move assignment");
    check_assigned_clean_state("saved-clean source invalidated set_cell");
    check(threw_fastxlsx_error([&] { (void)target_handle.has_pending_changes(); }),
        "overwritten saved-clean target handle should be invalid after move assignment");
    check_assigned_clean_state("saved-clean target invalidated has_pending_changes");
    check(threw_fastxlsx_error([&] {
        (void)target_handle.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    }), "overwritten saved-clean target handle should reject ranged sparse reads");
    check_assigned_clean_state("saved-clean target invalidated ranged sparse_cells");
    check(threw_fastxlsx_error([&] {
        target_handle.set_cell(2, 1,
            fastxlsx::CellValue::text("stale-saved-clean-target-assignment-write"));
    }), "overwritten saved-clean target handle should reject stale writes");
    check_assigned_clean_state("saved-clean target invalidated set_cell");
    check(threw_fastxlsx_error([&] { target_handle.erase_cell(1, 1); }),
        "overwritten saved-clean target handle should reject stale erases");
    check_assigned_clean_state("saved-clean target invalidated erase_cell");

    target_editor.save_as(output);
    check(target_editor.last_edit_error() == source_prior_error,
        "save_as after saved-clean move-assignment stale handles should preserve diagnostic");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "saved-clean-assigned-source-before",
        "move-assigned output should keep the assigned saved source value");
    check_not_contains(worksheet_xml, "discarded-saved-clean-target-before",
        "move-assigned output should discard overwritten saved target value");
    check_not_contains(worksheet_xml, "stale-saved-clean-source-assignment-write",
        "invalidated saved-clean source handle should not write stale data");
    check_not_contains(worksheet_xml, "stale-saved-clean-target-assignment-write",
        "invalidated saved-clean target handle should not write stale data");
    check_not_contains(worksheet_xml, "saved-clean-assigned-source-sentinel",
        "failed source diagnostic seed should not write invalid-coordinate data");
    check_not_contains(worksheet_xml, "discarded-saved-clean-target-sentinel",
        "failed target diagnostic seed should not write discarded invalid-reference data");
}

void test_public_worksheet_editor_readonly_handle_invalidated_after_owner_move_preserves_clean_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-move-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-readonly-move-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor old_handle = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = old_handle.get_cell(1, 1);
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "read-only owner-move setup should materialize the source cell");
    check(!old_handle.has_pending_changes(),
        "read-only owner-move setup should keep the handle clean");
    check(!editor.has_pending_changes(),
        "read-only owner-move setup should keep the editor clean");

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::text("readonly-move-diagnostic-sentinel")}});
    }), "read-only owner-move setup should seed a prior public diagnostic");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "read-only owner-move setup should record a prior diagnostic");

    fastxlsx::WorkbookEditor moved = std::move(editor);
    check(moved.last_edit_error() == prior_error,
        "read-only owner move should transfer the prior diagnostic");
    check(!editor.last_edit_error().has_value(),
        "read-only moved-from editor should expose no diagnostic");

    const auto check_moved_readonly_state = [&] (std::string_view prefix) {
        check(moved.last_edit_error() == prior_error,
            std::string(prefix) + " should preserve moved-to last_edit_error");
        check(!moved.has_pending_changes(),
            std::string(prefix) + " should keep the moved-to editor clean");
        check(moved.pending_change_count() == 0,
            std::string(prefix) + " should not queue public edits");
        check(moved.pending_materialized_worksheet_names().empty(),
            std::string(prefix) + " should keep dirty materialized names empty");
        check(moved.pending_materialized_cell_count() == 0,
            std::string(prefix) + " should keep dirty materialized cell count clear");
        check(moved.estimated_pending_materialized_memory_usage() == 0,
            std::string(prefix) + " should keep dirty materialized memory clear");
        check(moved.pending_worksheet_edits().empty(),
            std::string(prefix) + " should keep worksheet edit summaries empty");

        fastxlsx::WorksheetEditor reacquired = moved.worksheet("Data");
        check(!reacquired.has_pending_changes(),
            std::string(prefix) + " should preserve the clean read-only session");
        const fastxlsx::CellValue moved_value = reacquired.get_cell(1, 1);
        check(moved_value.kind() == fastxlsx::CellValueKind::Text &&
                moved_value.text_value() == "placeholder-a1",
            std::string(prefix) + " should preserve the source-backed value");
    };

    check(threw_fastxlsx_error([&] { (void)old_handle.has_pending_changes(); }),
        "read-only handle should be invalid after owner move");
    check_moved_readonly_state("read-only invalidated has_pending_changes");
    check(threw_fastxlsx_error([&] { (void)old_handle.get_cell("A1"); }),
        "read-only handle should reject reads after owner move");
    check_moved_readonly_state("read-only invalidated get_cell");
    check(threw_fastxlsx_error([&] {
        (void)old_handle.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    }), "read-only handle should reject ranged sparse reads after owner move");
    check_moved_readonly_state("read-only invalidated ranged sparse_cells");
    check(threw_fastxlsx_error([&] {
        old_handle.set_cell(2, 1,
            fastxlsx::CellValue::text("stale-readonly-move-write"));
    }), "read-only handle should reject stale writes after owner move");
    check_moved_readonly_state("read-only invalidated set_cell");

    moved.save_as(output);
    check(moved.last_edit_error() == prior_error,
        "read-only no-op save_as after stale-handle failures should preserve diagnostic");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "read-only no-op save_as after owner-move stale handles should copy source entries");
}

void test_public_worksheet_editor_readonly_handles_invalidated_after_move_assignment_preserve_source_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-assign-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-assign-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-readonly-assign-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor source_editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor source_handle = source_editor.worksheet("Data");
    const fastxlsx::CellValue source_value = source_handle.get_cell(1, 1);
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "read-only move-assignment source setup should materialize the source cell");
    check(!source_handle.has_pending_changes(),
        "read-only move-assignment source setup should keep the handle clean");
    check(!source_editor.has_pending_changes(),
        "read-only move-assignment source setup should keep the editor clean");
    check(threw_fastxlsx_error([&] {
        source_editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::text("readonly-source-diagnostic-sentinel")}});
    }), "read-only source setup should seed a prior public diagnostic");
    const std::optional<std::string> source_prior_error =
        source_editor.last_edit_error();
    check(source_prior_error.has_value(),
        "read-only source setup should record a diagnostic before assignment");

    fastxlsx::WorkbookEditor target_editor = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::WorksheetEditor target_handle = target_editor.worksheet("Data");
    const fastxlsx::CellValue target_value = target_handle.get_cell(1, 1);
    check(target_value.kind() == fastxlsx::CellValueKind::Text &&
            target_value.text_value() == "placeholder-a1",
        "read-only move-assignment target setup should materialize its source cell");
    check(!target_handle.has_pending_changes(),
        "read-only move-assignment target setup should keep the overwritten handle clean");
    check(threw_fastxlsx_error([&] {
        target_editor.rename_sheet("Missing", "DiscardedTarget");
    }), "read-only target setup should seed a diagnostic before overwrite");
    check(target_editor.last_edit_error().has_value(),
        "read-only overwritten target should have a discarded diagnostic");

    target_editor = std::move(source_editor);
    check(target_editor.last_edit_error() == source_prior_error,
        "read-only move assignment should keep the assigned source diagnostic");
    check(!source_editor.last_edit_error().has_value(),
        "read-only move-assigned-from editor should expose no diagnostic");

    const auto check_assigned_readonly_state = [&] (std::string_view prefix) {
        check(target_editor.last_edit_error() == source_prior_error,
            std::string(prefix) + " should preserve assigned source last_edit_error");
        check(!target_editor.has_pending_changes(),
            std::string(prefix) + " should keep assigned editor clean");
        check(target_editor.pending_change_count() == 0,
            std::string(prefix) + " should not queue public edits");
        check(target_editor.pending_materialized_worksheet_names().empty(),
            std::string(prefix) + " should keep assigned dirty names empty");
        check(target_editor.pending_materialized_cell_count() == 0,
            std::string(prefix) + " should keep assigned dirty cell count clear");
        check(target_editor.estimated_pending_materialized_memory_usage() == 0,
            std::string(prefix) + " should keep assigned dirty memory clear");
        check(target_editor.pending_worksheet_edits().empty(),
            std::string(prefix) + " should keep assigned summaries empty");

        fastxlsx::WorksheetEditor reacquired = target_editor.worksheet("Data");
        check(!reacquired.has_pending_changes(),
            std::string(prefix) + " should preserve the assigned read-only session");
        const fastxlsx::CellValue assigned_value = reacquired.get_cell(1, 1);
        check(assigned_value.kind() == fastxlsx::CellValueKind::Text &&
                assigned_value.text_value() == "placeholder-a1",
            std::string(prefix) + " should preserve assigned source cell value");
    };

    check(threw_fastxlsx_error([&] { (void)source_handle.has_pending_changes(); }),
        "read-only source handle should be invalid after move assignment");
    check_assigned_readonly_state("read-only source invalidated has_pending_changes");
    check(threw_fastxlsx_error([&] { (void)source_handle.get_cell("A1"); }),
        "read-only source handle should reject reads after move assignment");
    check_assigned_readonly_state("read-only source invalidated get_cell");
    check(threw_fastxlsx_error([&] {
        source_handle.set_cell(2, 1,
            fastxlsx::CellValue::text("stale-readonly-source-assignment-write"));
    }), "read-only source handle should reject stale writes after move assignment");
    check_assigned_readonly_state("read-only source invalidated set_cell");
    check(threw_fastxlsx_error([&] { (void)target_handle.has_pending_changes(); }),
        "overwritten read-only target handle should be invalid after move assignment");
    check_assigned_readonly_state("read-only target invalidated has_pending_changes");
    check(threw_fastxlsx_error([&] {
        (void)target_handle.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    }), "overwritten read-only target handle should reject ranged sparse reads");
    check_assigned_readonly_state("read-only target invalidated ranged sparse_cells");
    check(threw_fastxlsx_error([&] {
        target_handle.set_cell(2, 1,
            fastxlsx::CellValue::text("stale-readonly-target-assignment-write"));
    }), "overwritten read-only target handle should reject stale writes");
    check_assigned_readonly_state("read-only target invalidated set_cell");

    target_editor.save_as(output);
    check(target_editor.last_edit_error() == source_prior_error,
        "read-only no-op save_as after move-assignment stale handles should preserve diagnostic");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "read-only no-op save_as after move-assignment stale handles should copy assigned source entries");
}

void test_public_worksheet_editor_set_cell_auto_flushes_on_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(sheet.name() == "Data",
        "public WorksheetEditor should expose the planned worksheet name");
    const std::optional<fastxlsx::CellValue> original = sheet.try_cell(1, 1);
    check(original.has_value() && original->kind() == fastxlsx::CellValueKind::Text &&
            original->text_value() == "placeholder-a1",
        "public WorksheetEditor should read a supported source cell");
    check(!sheet.try_cell(3, 3).has_value(),
        "public WorksheetEditor should report missing sparse cells as nullopt");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("public-random-edit"));
    check(editor.has_pending_changes(),
        "dirty public WorksheetEditor edits should make save_as pending");
    check(editor.pending_change_count() == 0,
        "dirty public WorksheetEditor edits should not queue a Patch handoff before save_as");

    editor.save_as(output);
    check(editor.pending_change_count() == 1,
        "save_as should auto-flush one dirty public WorksheetEditor session");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "public-random-edit",
        "public WorksheetEditor set_cell should persist through save_as");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "public WorksheetEditor save_as should refresh the sparse worksheet dimension");
}

void test_public_try_worksheet_missing_returns_empty_and_preserves_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-try-worksheet-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-try-worksheet-missing-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Missing", {{fastxlsx::CellValue::number(1.0)}});
    }), "precondition failed replacement should record a public diagnostic");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "precondition failed replacement should leave last_edit_error populated");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();

    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        "missing try_worksheet",
        "after failed replacement precondition",
        "Missing",
        prior_error);

    const std::optional<fastxlsx::WorksheetEditor> missing =
        editor.try_worksheet("Missing");
    check(!missing.has_value(),
        "try_worksheet should return empty for a missing planned worksheet");
    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        "missing try_worksheet",
        "after missing lookup",
        "Missing",
        prior_error);

    editor.save_as(output);

    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        "missing try_worksheet",
        "after no-op save_as",
        "Missing",
        prior_error);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as after missing try_worksheet should copy source entries");
}

void test_public_worksheet_missing_throws_and_preserves_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-missing-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Missing", {{fastxlsx::CellValue::number(1.0)}});
    }), "precondition failed replacement should record a public diagnostic before worksheet");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "precondition failed replacement should leave last_edit_error populated before worksheet");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();

    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        "missing worksheet",
        "after failed replacement precondition",
        "Missing",
        prior_error);

    bool missing_threw = false;
    try {
        (void)editor.worksheet("Missing");
    } catch (const fastxlsx::FastXlsxError& error) {
        missing_threw = true;
        check_contains(error.what(), "Missing",
            "worksheet missing-sheet exception should identify the sheet name");
    }
    check(missing_threw,
        "worksheet should throw for a missing planned worksheet");
    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        "missing worksheet",
        "after missing lookup",
        "Missing",
        prior_error);

    editor.save_as(output);

    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        "missing worksheet",
        "after no-op save_as",
        "Missing",
        prior_error);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as after missing worksheet should copy source entries");
}

void test_public_try_worksheet_existing_handle_reads_mutates_and_saves()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-try-worksheet-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-try-worksheet-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    std::optional<fastxlsx::WorksheetEditor> maybe_sheet = editor.try_worksheet("Data");
    check(maybe_sheet.has_value(),
        "try_worksheet should return a handle for an existing planned worksheet");
    if (!maybe_sheet.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor& sheet = *maybe_sheet;
    const fastxlsx::CellValue old_a1 = sheet.get_cell(1, 1);
    check(old_a1.kind() == fastxlsx::CellValueKind::Text &&
            old_a1.text_value() == "placeholder-a1",
        "get_cell should read an existing source-backed cell");
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("try-worksheet-updated"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "try-worksheet-updated",
        "try_worksheet handle mutation should persist through save_as");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "try_worksheet handle mutation should replace the old value");
}

void test_public_worksheet_editor_normalizes_explicit_default_style_id()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-default-style-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-default-style-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const fastxlsx::CellValue explicit_default_style =
        fastxlsx::CellValue::text("default-style-cell").with_style(fastxlsx::StyleId {});
    check(explicit_default_style.has_style() && explicit_default_style.style_id().value() == 0,
        "test precondition should use an explicit default StyleId");

    sheet.set_cell(1, 1, explicit_default_style);
    const fastxlsx::CellValue read_back = sheet.get_cell(1, 1);
    check(read_back.kind() == fastxlsx::CellValueKind::Text &&
            read_back.text_value() == "default-style-cell",
        "WorksheetEditor should preserve the value when normalizing default StyleId");
    check(!read_back.has_style(),
        "WorksheetEditor should normalize explicit StyleId{0} to no style handle");

    const std::optional<fastxlsx::CellValue> maybe_read_back = sheet.try_cell(1, 1);
    check(maybe_read_back.has_value() && !maybe_read_back->has_style(),
        "try_cell should expose the normalized no-style value");

    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells();
    check(!cells.empty() && cells[0].reference.row == 1 && cells[0].reference.column == 1,
        "sparse_cells should include the edited default-style cell");
    check(!cells.empty() && !cells[0].value.has_style(),
        "sparse_cells should expose default StyleId normalization");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1" t="inlineStr"><is><t>default-style-cell</t></is></c>)",
        "dirty projection should write the normalized value without an s attribute");
    check_not_contains(worksheet_xml, R"(s="0")",
        "dirty projection should not serialize an explicit default style attribute");
}

void test_public_worksheet_editor_rejects_non_default_style_id_without_mutation()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-non-default-style-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-non-default-style-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        const std::filesystem::path style_source =
            artifact("fastxlsx-workbook-editor-public-non-default-style-provider.xlsx");
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter data = writer.add_worksheet("StyleProvider");
        data.append_row({fastxlsx::CellView::number(1.0).with_style(non_default_style)});
        writer.close();
    }
    check(non_default_style.value() != 0,
        "test precondition should use a non-default StyleId");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before = sheet.cell_count();
    const std::size_t memory_before = sheet.estimated_memory_usage();
    const std::optional<fastxlsx::CellValue> a1_before = sheet.try_cell(1, 1);
    const std::optional<fastxlsx::CellValue> c3_before = sheet.try_cell(3, 3);
    const std::vector<fastxlsx::WorksheetCellSnapshot> sparse_before = sheet.sparse_cells();

    const fastxlsx::CellValue styled_value =
        fastxlsx::CellValue::text("must-not-write").with_style(non_default_style);
    check(styled_value.has_style() && styled_value.style_id().value() != 0,
        "test precondition should pass a styled CellValue");

    check(threw_fastxlsx_error([&] { sheet.set_cell(3, 3, styled_value); }),
        "WorksheetEditor should reject non-default StyleId values");
    check(editor.last_edit_error().has_value(),
        "style rejection should update WorkbookEditor::last_edit_error");
    check(editor.last_edit_error()->find("does not support non-default StyleId")
            != std::string::npos,
        "style rejection diagnostic should explain the unsupported StyleId");

    check(!sheet.has_pending_changes(),
        "rejected non-default StyleId should not dirty the materialized sheet");
    check(!editor.has_pending_changes(),
        "rejected non-default StyleId should not dirty the workbook editor");
    check(editor.pending_change_count() == 0,
        "rejected non-default StyleId should not queue a pending edit");
    check(sheet.cell_count() == cell_count_before,
        "rejected non-default StyleId should not change the sparse cell count");
    check(sheet.estimated_memory_usage() == memory_before,
        "rejected non-default StyleId should not change sparse-store memory usage");

    const std::optional<fastxlsx::CellValue> a1_after = sheet.try_cell(1, 1);
    const std::optional<fastxlsx::CellValue> c3_after = sheet.try_cell(3, 3);
    check(a1_before.has_value() && a1_after.has_value()
            && a1_after->kind() == a1_before->kind()
            && a1_after->text_value() == a1_before->text_value(),
        "rejected non-default StyleId should not alter existing cells");
    check(!c3_before.has_value() && !c3_after.has_value(),
        "rejected non-default StyleId should not create the target cell");

    const std::vector<fastxlsx::WorksheetCellSnapshot> sparse_after = sheet.sparse_cells();
    check(sparse_after.size() == sparse_before.size(),
        "rejected non-default StyleId should not change sparse snapshot size");
    if (sparse_after.size() == sparse_before.size()) {
        for (std::size_t index = 0; index < sparse_before.size(); ++index) {
            check(sparse_after[index].reference.row == sparse_before[index].reference.row
                    && sparse_after[index].reference.column
                        == sparse_before[index].reference.column
                    && sparse_after[index].value.kind() == sparse_before[index].value.kind(),
                "rejected non-default StyleId should preserve sparse snapshot entries");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save after non-default StyleId rejection should copy source entries");
}

void test_public_worksheet_editor_rejects_non_default_style_id_a1_without_mutation()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-non-default-style-a1-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-non-default-style-a1-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        const std::filesystem::path style_source =
            artifact("fastxlsx-workbook-editor-public-non-default-style-a1-provider.xlsx");
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter data = writer.add_worksheet("StyleProvider");
        data.append_row({fastxlsx::CellView::number(1.0).with_style(non_default_style)});
        writer.close();
    }
    check(non_default_style.value() != 0,
        "test precondition should use a non-default StyleId for the A1 overload");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before = sheet.cell_count();
    const std::size_t memory_before = sheet.estimated_memory_usage();
    const fastxlsx::CellValue a1_before = sheet.get_cell("A1");
    const std::vector<fastxlsx::WorksheetCellSnapshot> sparse_before = sheet.sparse_cells();

    const fastxlsx::CellValue styled_value =
        fastxlsx::CellValue::text("must-not-overwrite-a1").with_style(non_default_style);
    check(styled_value.has_style() && styled_value.style_id().value() != 0,
        "test precondition should pass a styled CellValue through the A1 overload");

    check(threw_fastxlsx_error([&] { sheet.set_cell("A1", styled_value); }),
        "WorksheetEditor A1 overload should reject non-default StyleId values");
    check(editor.last_edit_error().has_value(),
        "A1 style rejection should update WorkbookEditor::last_edit_error");
    check(editor.last_edit_error()->find("does not support non-default StyleId")
            != std::string::npos,
        "A1 style rejection diagnostic should explain the unsupported StyleId");

    check(!sheet.has_pending_changes(),
        "A1 non-default StyleId rejection should not dirty the materialized sheet");
    check(!editor.has_pending_changes(),
        "A1 non-default StyleId rejection should not dirty the workbook editor");
    check(editor.pending_change_count() == 0,
        "A1 non-default StyleId rejection should not queue a pending edit");
    check(sheet.cell_count() == cell_count_before,
        "A1 non-default StyleId rejection should not change the sparse cell count");
    check(sheet.estimated_memory_usage() == memory_before,
        "A1 non-default StyleId rejection should not change sparse-store memory usage");

    const fastxlsx::CellValue a1_after = sheet.get_cell("A1");
    check(a1_after.kind() == a1_before.kind()
            && a1_after.text_value() == a1_before.text_value(),
        "A1 non-default StyleId rejection should not overwrite the existing cell");

    const std::vector<fastxlsx::WorksheetCellSnapshot> sparse_after = sheet.sparse_cells();
    check(sparse_after.size() == sparse_before.size(),
        "A1 non-default StyleId rejection should not change sparse snapshot size");
    if (sparse_after.size() == sparse_before.size()) {
        for (std::size_t index = 0; index < sparse_before.size(); ++index) {
            check(sparse_after[index].reference.row == sparse_before[index].reference.row
                    && sparse_after[index].reference.column
                        == sparse_before[index].reference.column
                    && sparse_after[index].value.kind() == sparse_before[index].value.kind(),
                "A1 non-default StyleId rejection should preserve sparse snapshot entries");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save after A1 non-default StyleId rejection should copy source entries");
}

void test_public_try_worksheet_reuses_options_and_blocks_replacement_mix()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-try-worksheet-options-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        std::optional<fastxlsx::WorksheetEditor> first = editor.try_worksheet("Data");
        check(first.has_value(),
            "try_worksheet should materialize the first matching worksheet session");

        fastxlsx::WorksheetEditorOptions mismatched_options;
        mismatched_options.max_cells = 100;
        check(threw_fastxlsx_error([&] {
            (void)editor.try_worksheet("Data", mismatched_options);
        }), "try_worksheet should reject option mismatch for an existing session");
        check(!editor.last_edit_error().has_value(),
            "try_worksheet option mismatch should not update last_edit_error");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("queued")}});
        check(threw_fastxlsx_error([&] {
            (void)editor.try_worksheet("Data");
        }), "try_worksheet should reject a worksheet with queued sheetData replacement");
        check(!editor.last_edit_error().has_value(),
            "try_worksheet replacement-mix rejection should not update last_edit_error");
        check(editor.pending_change_count() == 1,
            "try_worksheet replacement-mix rejection should preserve queued edits");
    }
}

void test_public_worksheet_editor_reacquire_reuses_dirty_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-reacquire-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-reacquire-output.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor first = editor.worksheet("Data", options);
    first.set_cell(1, 1, fastxlsx::CellValue::text("reacquire-first"));

    check(first.has_pending_changes(),
        "first handle should be dirty after the first public mutation");
    check(editor.has_pending_changes(),
        "dirty materialized session should make the editor pending");
    check(editor.pending_change_count() == 0,
        "dirty materialized session should not queue a Patch handoff before save_as");

    std::optional<fastxlsx::WorksheetEditor> maybe_second =
        editor.try_worksheet("Data", options);
    check(maybe_second.has_value(),
        "try_worksheet should reacquire an existing materialized session with matching options");
    if (!maybe_second.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor& second = *maybe_second;
    const fastxlsx::CellValue second_reads_first = second.get_cell(1, 1);
    check(second_reads_first.kind() == fastxlsx::CellValueKind::Text &&
            second_reads_first.text_value() == "reacquire-first",
        "reacquired try_worksheet handle should see dirty state from the first handle");
    check(second.has_pending_changes(),
        "reacquired handle should report the shared dirty session");

    second.set_cell(2, 1, fastxlsx::CellValue::text("reacquire-second"));
    const fastxlsx::CellValue first_reads_second = first.get_cell(2, 1);
    check(first_reads_second.kind() == fastxlsx::CellValueKind::Text &&
            first_reads_second.text_value() == "reacquire-second",
        "first handle should see dirty state written through the reacquired handle");

    fastxlsx::WorksheetEditor third = editor.worksheet("Data", options);
    const fastxlsx::CellValue third_reads_second = third.get_cell(2, 1);
    check(third_reads_second.kind() == fastxlsx::CellValueKind::Text &&
            third_reads_second.text_value() == "reacquire-second",
        "worksheet should also reacquire the same dirty session with matching options");

    check(editor.pending_change_count() == 0,
        "reacquiring matching handles should not queue additional public edits");
    check(!editor.last_edit_error().has_value(),
        "successful matching reacquire should not update last_edit_error");

    editor.save_as(output);
    check(!first.has_pending_changes() && !second.has_pending_changes() &&
            !third.has_pending_changes(),
        "successful save_as should clear the shared dirty session for all handles");
    check(editor.pending_change_count() == 1,
        "successful save_as should flush the reused session exactly once");
    check(editor.pending_materialized_worksheet_names().empty(),
        "successful save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "reacquire-first",
        "flushed output should include the first handle mutation");
    check_contains(worksheet_xml, "reacquire-second",
        "flushed output should include the reacquired handle mutation");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "reused dirty session should not reload and restore the old A1 value");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "reused dirty session should not reload and restore the old A2 value");
}

void test_public_worksheet_editor_reacquire_after_save_reuses_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-reacquire-after-save-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-reacquire-after-save-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-reacquire-after-save-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor first = editor.worksheet("Data", options);
    first.set_cell(1, 1, fastxlsx::CellValue::text("reacquire-after-save-first"));
    editor.save_as(first_output);

    check(!first.has_pending_changes(),
        "successful first save_as should clear the original handle dirty state");
    check(editor.pending_change_count() == 1,
        "first save_as should flush one materialized handoff");

    std::optional<fastxlsx::WorksheetEditor> maybe_second =
        editor.try_worksheet("Data", options);
    check(maybe_second.has_value(),
        "try_worksheet should reacquire a saved clean materialized session");
    if (!maybe_second.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor& second = *maybe_second;
    const fastxlsx::CellValue second_reads_saved = second.get_cell(1, 1);
    check(second_reads_saved.kind() == fastxlsx::CellValueKind::Text &&
            second_reads_saved.text_value() == "reacquire-after-save-first",
        "reacquire after save_as should keep materialized state instead of reloading source");
    check(!second.has_pending_changes(),
        "reacquired post-save handle should see the shared clean session");
    check(!editor.last_edit_error().has_value(),
        "successful post-save reacquire should not update last_edit_error");

    second.set_cell(2, 1, fastxlsx::CellValue::text("reacquire-after-save-second"));
    const fastxlsx::CellValue first_reads_second = first.get_cell(2, 1);
    check(first_reads_second.kind() == fastxlsx::CellValueKind::Text &&
            first_reads_second.text_value() == "reacquire-after-save-second",
        "original handle should see post-save edits made through the reacquired handle");

    editor.save_as(second_output);
    check(editor.pending_change_count() == 2,
        "second save_as should flush the post-save reacquired dirty session once");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, "reacquire-after-save-first",
        "first output should contain the first saved materialized value");
    check_not_contains(first_xml, "reacquire-after-save-second",
        "first output should not contain the later post-save mutation");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, "reacquire-after-save-first",
        "second output should keep the previously saved materialized value");
    check_contains(second_xml, "reacquire-after-save-second",
        "second output should contain the post-save reacquired handle mutation");
    check_not_contains(second_xml, "placeholder-a1",
        "post-save reacquire should not restore the original source A1 value");
    check_not_contains(second_xml, "placeholder-a2",
        "post-save reacquire should not restore the original source A2 value");
}

void test_public_worksheet_editor_post_save_reacquire_preserves_clean_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-reacquire-diagnostics-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-reacquire-diagnostics-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-reacquire-diagnostics-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor first = editor.worksheet("Data", options);
    first.set_cell(1, 1, fastxlsx::CellValue::text("diagnostic-first"));
    editor.save_as(first_output);

    check(editor.pending_change_count() == 1,
        "first save_as should record one materialized Patch handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "first save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "first save_as should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "first save_as should clear dirty materialized memory estimate");

    fastxlsx::WorksheetEditor second = editor.worksheet("Data", options);
    const fastxlsx::CellValue second_reads_first = second.get_cell(1, 1);
    check(second_reads_first.kind() == fastxlsx::CellValueKind::Text &&
            second_reads_first.text_value() == "diagnostic-first",
        "post-save worksheet reacquire should reuse the saved materialized state");
    check(!second.has_pending_changes(),
        "post-save worksheet reacquire should return the clean shared session");
    check(editor.pending_change_count() == 1,
        "clean post-save reacquire should not add a public edit count");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean post-save reacquire should not add dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "clean post-save reacquire should not add dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "clean post-save reacquire should not add dirty materialized memory");
    check(!editor.last_edit_error().has_value(),
        "clean post-save worksheet reacquire should not update last_edit_error");

    second.set_cell(2, 2, fastxlsx::CellValue::text("diagnostic-second"));
    check(first.has_pending_changes() && second.has_pending_changes(),
        "post-save mutation through reacquired handle should dirty the shared session");
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "post-save mutation should add one dirty materialized worksheet name");
    }
    check(editor.pending_materialized_cell_count() == second.cell_count(),
        "dirty materialized cell count should match the reacquired session");
    check(editor.estimated_pending_materialized_memory_usage() ==
            second.estimated_memory_usage(),
        "dirty materialized memory estimate should match the reacquired session");
    check(editor.pending_change_count() == 1,
        "dirty post-save session should not increment public edit count before save_as");

    editor.save_as(second_output);
    check(editor.pending_change_count() == 2,
        "second save_as should record a second materialized Patch handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second save_as should clear dirty materialized names again");
    check(editor.pending_materialized_cell_count() == 0,
        "second save_as should clear dirty materialized cell count again");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second save_as should clear dirty materialized memory estimate again");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, "diagnostic-first",
        "second output should keep the saved materialized value");
    check_contains(second_xml, "diagnostic-second",
        "second output should include the post-save diagnostic mutation");
}

void test_public_worksheet_editor_post_save_option_mismatch_preserves_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-post-save-option-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-post-save-option-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-post-save-option-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;
    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 9;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("post-save-option-first"));
    editor.save_as(first_output);

    check(!sheet.has_pending_changes(),
        "first save_as should leave the saved materialized session clean");
    check(editor.pending_change_count() == 1,
        "first save_as should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "first save_as should leave no dirty materialized names");

    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("Data", mismatched_options);
    }), "post-save try_worksheet should still reject mismatched materialization options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data", mismatched_options);
    }), "post-save worksheet should still reject mismatched materialization options");
    check(!editor.last_edit_error().has_value(),
        "post-save option mismatch should not update last_edit_error");
    check(editor.pending_change_count() == 1,
        "post-save option mismatch should not queue another public edit");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-save option mismatch should not dirty the saved materialized session");
    check(editor.pending_materialized_cell_count() == 0,
        "post-save option mismatch should keep dirty materialized cell count clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-save option mismatch should keep dirty materialized memory clear");

    const fastxlsx::CellValue preserved_value = sheet.get_cell(1, 1);
    check(preserved_value.kind() == fastxlsx::CellValueKind::Text &&
            preserved_value.text_value() == "post-save-option-first",
        "post-save option mismatch should preserve the saved materialized value");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    const fastxlsx::CellValue reacquired_value = reacquired.get_cell(1, 1);
    check(reacquired_value.kind() == fastxlsx::CellValueKind::Text &&
            reacquired_value.text_value() == "post-save-option-first",
        "matching options should still reacquire the saved session after mismatch failures");

    reacquired.set_cell(2, 2, fastxlsx::CellValue::text("post-save-option-second"));
    check(sheet.has_pending_changes(),
        "valid post-mismatch mutation should dirty the original shared handle");
    editor.save_as(second_output);
    check(editor.pending_change_count() == 2,
        "valid post-mismatch save_as should record a second materialized handoff");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, "post-save-option-first",
        "second output should keep the value saved before option mismatch failures");
    check_contains(second_xml, "post-save-option-second",
        "second output should include the later valid post-mismatch mutation");
    check_not_contains(second_xml, "placeholder-a1",
        "post-save option mismatch should not reload stale source A1");
}

void test_public_worksheet_editor_post_save_summary_tracks_reacquire_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-post-save-summary-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-post-save-summary-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-post-save-summary-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("post-save-summary-first"));

    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "dirty materialized session should appear in pending worksheet summaries before save");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "pre-save summary should report the source sheet name");
            check(summary.planned_name == "Data",
                "pre-save summary should report the planned sheet name");
            check(!summary.renamed,
                "pre-save materialized-only summary should not report a rename");
            check(!summary.sheet_data_replaced,
                "pre-save materialized-only summary should not report a queued replacement");
            check(summary.materialized_dirty,
                "pre-save summary should report dirty materialized state");
            check(summary.replacement_cell_count == 0,
                "pre-save materialized-only summary should report zero replacement cells");
            check(summary.estimated_replacement_memory_usage == 0,
                "pre-save materialized-only summary should report zero replacement memory");
            check(summary.materialized_cell_count == sheet.cell_count(),
                "pre-save summary materialized cell count should match the dirty session");
            check(summary.estimated_materialized_memory_usage ==
                    sheet.estimated_memory_usage(),
                "pre-save summary materialized memory should match the dirty session");
        }
    }
    check(editor.pending_change_count() == 0,
        "dirty materialized summary should not increment public edit count before save_as");

    editor.save_as(first_output);
    check(editor.pending_change_count() == 1,
        "first save_as should record one materialized Patch handoff");
    check(!sheet.has_pending_changes(),
        "first save_as should clear the original handle dirty flag");
    check(editor.pending_worksheet_edits().empty(),
        "successful materialized save_as should remove the dirty-only worksheet summary");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    const fastxlsx::CellValue reacquired_first = reacquired.get_cell(1, 1);
    check(reacquired_first.kind() == fastxlsx::CellValueKind::Text &&
            reacquired_first.text_value() == "post-save-summary-first",
        "clean post-save reacquire should reuse the saved materialized value");
    check(!reacquired.has_pending_changes(),
        "clean post-save reacquire should keep the shared session clean");
    check(editor.pending_worksheet_edits().empty(),
        "clean post-save reacquire should keep pending worksheet summaries empty");

    reacquired.set_cell(2, 2, fastxlsx::CellValue::text("post-save-summary-second"));
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "post-save mutation should re-add one dirty materialized summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "post-save dirty summary should still identify the same sheet");
            check(!summary.sheet_data_replaced,
                "post-save dirty summary should not expose the prior materialized handoff as replacement");
            check(summary.materialized_dirty,
                "post-save dirty summary should mark the reacquired session dirty");
            check(summary.materialized_cell_count == reacquired.cell_count(),
                "post-save dirty summary cell count should match the reacquired session");
            check(summary.estimated_materialized_memory_usage ==
                    reacquired.estimated_memory_usage(),
                "post-save dirty summary memory should match the reacquired session");
        }
    }
    check(editor.pending_change_count() == 1,
        "post-save dirty summary should not increment public edit count before second save_as");

    editor.save_as(second_output);
    check(editor.pending_change_count() == 2,
        "second save_as should record the post-save materialized handoff");
    check(editor.pending_worksheet_edits().empty(),
        "second save_as should clear dirty-only worksheet summaries again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, "post-save-summary-first",
        "first output should contain the first materialized edit");
    check_not_contains(first_xml, "post-save-summary-second",
        "first output should not contain the later post-save summary edit");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, "post-save-summary-first",
        "second output should keep the first materialized value");
    check_contains(second_xml, "post-save-summary-second",
        "second output should include the post-save summary mutation");
}

void test_public_worksheet_editor_post_save_summary_preserves_rename_context()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-summary-rename-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-summary-rename-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-summary-rename-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "SummaryRenamed");
    fastxlsx::WorksheetEditor renamed = editor.worksheet("SummaryRenamed", options);
    renamed.set_cell(1, 1, fastxlsx::CellValue::text("summary-rename-first"));

    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "renamed dirty materialized session should create one worksheet summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "rename/materialized summary should report the source sheet name");
            check(summary.planned_name == "SummaryRenamed",
                "rename/materialized summary should report the planned sheet name");
            check(summary.renamed,
                "rename/materialized summary should report the rename");
            check(!summary.sheet_data_replaced,
                "rename/materialized summary should not report whole-sheet replacement");
            check(summary.materialized_dirty,
                "rename/materialized summary should report dirty materialized state before save");
            check(summary.materialized_cell_count == renamed.cell_count(),
                "rename/materialized summary should expose dirty session cell count before save");
            check(summary.estimated_materialized_memory_usage ==
                    renamed.estimated_memory_usage(),
                "rename/materialized summary should expose dirty session memory before save");
        }
    }

    editor.save_as(first_output);
    check(editor.pending_change_count() == 2,
        "rename plus first materialized flush should record two public handoffs");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "successful save_as should preserve the rename-only worksheet summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" &&
                    summary.planned_name == "SummaryRenamed",
                "post-save rename-only summary should keep source and planned names");
            check(summary.renamed,
                "post-save summary should keep the rename flag");
            check(!summary.sheet_data_replaced,
                "post-save rename-only summary should not report replacement");
            check(!summary.materialized_dirty,
                "post-save rename-only summary should clear materialized dirty flag");
            check(summary.materialized_cell_count == 0,
                "post-save rename-only summary should clear materialized cell count");
            check(summary.estimated_materialized_memory_usage == 0,
                "post-save rename-only summary should clear materialized memory");
        }
    }

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("SummaryRenamed", options);
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "summary-rename-first",
        "post-save reacquire should reuse the saved renamed materialized state");
    check(!reacquired.has_pending_changes(),
        "post-save reacquire of renamed session should stay clean");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1 && summaries[0].renamed &&
                !summaries[0].materialized_dirty,
            "clean post-save reacquire should keep only the rename summary");
    }

    reacquired.set_cell(2, 2, fastxlsx::CellValue::text("summary-rename-second"));
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "post-save mutation on renamed session should update the existing summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.renamed,
                "post-save dirty renamed summary should keep the rename flag");
            check(summary.materialized_dirty,
                "post-save dirty renamed summary should re-add materialized dirty state");
            check(summary.materialized_cell_count == reacquired.cell_count(),
                "post-save dirty renamed summary should expose current cell count");
            check(summary.estimated_materialized_memory_usage ==
                    reacquired.estimated_memory_usage(),
                "post-save dirty renamed summary should expose current memory");
        }
    }

    editor.save_as(second_output);
    check(editor.pending_change_count() == 3,
        "second materialized flush on renamed session should record a third handoff");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1 && summaries[0].renamed &&
                !summaries[0].materialized_dirty,
            "second save_as should return to a rename-only summary");
    }

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="SummaryRenamed")",
        "first output workbook catalog should contain the planned renamed sheet");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"), "summary-rename-first",
        "first output should contain the first renamed materialized edit");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "summary-rename-second",
        "first output should not contain the later renamed materialized edit");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="SummaryRenamed")",
        "second output workbook catalog should keep the planned renamed sheet");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"), "summary-rename-first",
        "second output should keep the first renamed materialized edit");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"), "summary-rename-second",
        "second output should include the later renamed materialized edit");
}

void test_public_worksheet_editor_failed_save_as_preserves_renamed_summary_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-summary-rename-failed-save-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-summary-rename-failed-save-output.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "FailedSaveSummary");
    fastxlsx::WorksheetEditor renamed = editor.worksheet("FailedSaveSummary", options);
    renamed.set_cell(1, 1, fastxlsx::CellValue::text("summary-rename-failed-save"));

    const std::size_t dirty_cell_count = renamed.cell_count();
    const std::size_t dirty_memory_usage = renamed.estimated_memory_usage();

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as source overwrite guard should reject before flushing renamed dirty session");
    check(renamed.has_pending_changes(),
        "rejected save_as should keep the renamed materialized session dirty");
    check(editor.pending_change_count() == 1,
        "rejected save_as should not count a materialized handoff after the rename");
    check(!editor.last_edit_error().has_value(),
        "rejected save_as should not update last_edit_error");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "FailedSaveSummary",
            "rejected save_as should preserve dirty materialized planned name");
    }
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "rejected save_as should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "rejected save_as should preserve dirty materialized memory estimate");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "rejected save_as should preserve one renamed dirty worksheet summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "rejected save_as summary should preserve source sheet name");
            check(summary.planned_name == "FailedSaveSummary",
                "rejected save_as summary should preserve planned sheet name");
            check(summary.renamed,
                "rejected save_as summary should preserve rename flag");
            check(!summary.sheet_data_replaced,
                "rejected save_as summary should not invent replacement diagnostics");
            check(summary.materialized_dirty,
                "rejected save_as summary should preserve materialized dirty flag");
            check(summary.materialized_cell_count == dirty_cell_count,
                "rejected save_as summary should preserve materialized cell count");
            check(summary.estimated_materialized_memory_usage == dirty_memory_usage,
                "rejected save_as summary should preserve materialized memory estimate");
        }
    }

    editor.save_as(output);
    check(!renamed.has_pending_changes(),
        "safe save_as after rejection should flush the renamed dirty session");
    check(editor.pending_change_count() == 2,
        "safe save_as should count the rename plus one materialized handoff");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1 && summaries[0].renamed &&
                !summaries[0].materialized_dirty,
            "safe save_as should leave a rename-only worksheet summary");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="FailedSaveSummary")",
        "safe output should keep the planned renamed sheet");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "summary-rename-failed-save",
        "safe output should include the materialized value after failed save_as recovery");
}

void test_public_worksheet_editor_renamed_materialized_diagnostics_follow_planned_name()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-diagnostics-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-diagnostics-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-diagnostics-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "RenamedDiagnostics");
    fastxlsx::WorksheetEditor renamed = editor.worksheet("RenamedDiagnostics", options);
    renamed.set_cell(1, 1, fastxlsx::CellValue::text("renamed-diagnostic-first"));

    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "RenamedDiagnostics",
            "dirty renamed materialized diagnostics should use the planned sheet name");
    }
    check(editor.pending_materialized_cell_count() == renamed.cell_count(),
        "dirty renamed materialized cell count should match the borrowed session");
    check(editor.estimated_pending_materialized_memory_usage() ==
            renamed.estimated_memory_usage(),
        "dirty renamed materialized memory should match the borrowed session");

    editor.save_as(first_output);
    check(!renamed.has_pending_changes(),
        "successful save_as should clean the renamed borrowed session");
    check(editor.pending_change_count() == 2,
        "rename plus materialized flush should count two public handoffs");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-save renamed diagnostics should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "post-save renamed diagnostics should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-save renamed diagnostics should clear dirty materialized memory");

    fastxlsx::WorksheetEditor reacquired =
        editor.worksheet("RenamedDiagnostics", options);
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "renamed-diagnostic-first",
        "clean renamed reacquire should reuse the saved materialized state");
    check(!reacquired.has_pending_changes(),
        "clean renamed reacquire should keep the session clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean renamed reacquire should not re-add dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "clean renamed reacquire should not re-add dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "clean renamed reacquire should not re-add dirty materialized memory");

    reacquired.set_cell(2, 2,
        fastxlsx::CellValue::text("renamed-diagnostic-second"));
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "RenamedDiagnostics",
            "post-save renamed mutation should re-add the planned dirty name");
    }
    check(editor.pending_materialized_cell_count() == reacquired.cell_count(),
        "post-save renamed mutation should expose current dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() ==
            reacquired.estimated_memory_usage(),
        "post-save renamed mutation should expose current dirty memory");

    editor.save_as(second_output);
    check(editor.pending_change_count() == 3,
        "second renamed materialized flush should count a third public handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second save_as should clear renamed dirty materialized names again");
    check(editor.pending_materialized_cell_count() == 0,
        "second save_as should clear renamed dirty materialized cells again");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second save_as should clear renamed dirty materialized memory again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="RenamedDiagnostics")",
        "first output should expose the planned renamed sheet name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "renamed-diagnostic-first",
        "first output should contain the first renamed diagnostic edit");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "renamed-diagnostic-second",
        "first output should not contain the later renamed diagnostic edit");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="RenamedDiagnostics")",
        "second output should keep the planned renamed sheet name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "renamed-diagnostic-first",
        "second output should keep the first renamed diagnostic edit");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "renamed-diagnostic-second",
        "second output should include the later renamed diagnostic edit");
}

void test_public_worksheet_editor_rename_back_materialized_diagnostics_use_source_name()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-materialized-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-materialized-output.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientData");
    editor.rename_sheet("TransientData", "Data");

    check(editor.pending_change_count() == 2,
        "rename-back before materialization should count both successful public edits");
    check(editor.has_worksheet("Data"),
        "rename-back before materialization should restore the source planned name");
    check(!editor.has_worksheet("TransientData"),
        "rename-back before materialization should remove the transient planned name");
    check(editor.pending_materialized_worksheet_names().empty(),
        "rename-back before materialization should not create dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "rename-back before materialization should not create dirty materialized cell diagnostics");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "rename-back before materialization should not create dirty materialized memory diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "rename-back before materialization should clear rename-only summaries");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    const fastxlsx::CellValue original = sheet.get_cell(1, 1);
    check(original.kind() == fastxlsx::CellValueKind::Text &&
            original.text_value() == "placeholder-a1",
        "rename-back materialization should read through the restored source name");

    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-materialized-source-name"));
    check(editor.pending_change_count() == 2,
        "dirty materialized state after rename-back should not increment public handoff count before save");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "dirty materialized diagnostics after rename-back should use the restored source name");
    }
    check(editor.pending_materialized_cell_count() == sheet.cell_count(),
        "dirty materialized cell count after rename-back should match the borrowed session");
    check(editor.estimated_pending_materialized_memory_usage() ==
            sheet.estimated_memory_usage(),
        "dirty materialized memory after rename-back should match the borrowed session");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "dirty materialized session after rename-back should create one summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "rename-back materialized summary should use restored source/planned names");
            check(!summary.renamed,
                "rename-back materialized summary should not remain marked as renamed");
            check(!summary.sheet_data_replaced,
                "rename-back materialized summary should not invent replacement diagnostics");
            check(summary.materialized_dirty,
                "rename-back materialized summary should report dirty materialized state");
            check(summary.materialized_cell_count == sheet.cell_count(),
                "rename-back materialized summary should report current cell count");
            check(summary.estimated_materialized_memory_usage ==
                    sheet.estimated_memory_usage(),
                "rename-back materialized summary should report current memory");
        }
    }

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "save_as after rename-back materialized edit should clear the borrowed dirty state");
    check(editor.pending_change_count() == 3,
        "save_as after rename-back should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after rename-back materialized edit should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after rename-back materialized edit should clear dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "save_as after rename-back materialized edit should clear dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after rename-back materialized edit should clear current summaries");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "rename-back materialized output should use the restored source name");
    check_not_contains(output_entries.at("xl/workbook.xml"), "TransientData",
        "rename-back materialized output should not leak the transient planned name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-materialized-source-name",
        "rename-back materialized output should contain the materialized edit");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "rename-back materialized output should replace the old source cell value");
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

void test_public_worksheet_editor_blocks_same_sheet_patch_operations()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-mixing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mixing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("materialized-public-state"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("blocked-public")}});
    }), "public replace_sheet_data should reject a materialized same sheet");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedPublicRename"); }),
        "public rename_sheet should reject a materialized same sheet");
    check(editor.last_edit_error().has_value(),
        "blocked same-sheet operations should record last_edit_error");

    editor.rename_sheet("Untouched", "OtherPublicName");
    check(!editor.last_edit_error().has_value(),
        "successful cross-sheet public edit should clear last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="OtherPublicName")",
        "cross-sheet rename should still save beside public WorksheetEditor edits");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "materialized-public-state",
        "public WorksheetEditor edit should auto-flush beside a cross-sheet edit");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "blocked-public",
        "blocked same-sheet replacement should not leak into output");
    check_not_contains(output_entries.at("xl/workbook.xml"), "BlockedPublicRename",
        "blocked same-sheet rename should not leak into workbook catalog");
}

void test_public_worksheet_editor_readonly_session_blocks_same_sheet_patch_operations()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-mixing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-readonly-mixing-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = sheet.get_cell(1, 1);
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "read-only same-sheet mixing setup should materialize the source value");
    check(!sheet.has_pending_changes(),
        "read-only same-sheet mixing setup should leave the borrowed handle clean");

    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    const std::size_t expected_cell_count = sheet.cell_count();
    const std::size_t expected_memory = sheet.estimated_memory_usage();

    const auto check_readonly_state = [&] (
        std::string_view stage, const std::optional<std::string>& expected_error) {
        check_public_materialization_failure_clean_state(
            editor,
            expected_source_names,
            expected_planned_names,
            expected_catalog,
            "read-only same-sheet patch mixing",
            stage,
            "Data",
            expected_error);
        check(!sheet.has_pending_changes(),
            std::string(stage) + " should keep the borrowed handle clean");
        check(sheet.cell_count() == expected_cell_count,
            std::string(stage) + " should preserve read-only sparse cell count");
        check(sheet.estimated_memory_usage() == expected_memory,
            std::string(stage) + " should preserve read-only sparse memory");
        const fastxlsx::CellValue current = sheet.get_cell(1, 1);
        check(current.kind() == fastxlsx::CellValueKind::Text &&
                current.text_value() == "placeholder-a1",
            std::string(stage) + " should preserve the source-backed cell value");
    };

    check_readonly_state("before rejected operations", std::nullopt);

    bool replacement_failed = false;
    try {
        editor.replace_sheet_data("Data",
            {{fastxlsx::CellValue::text("blocked-readonly-replacement")}});
    } catch (const fastxlsx::FastXlsxError& error) {
        replacement_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "read-only same-sheet replacement should populate last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "read-only same-sheet replacement diagnostic should match thrown error");
            check_contains(*last_error,
                "cannot replace sheet data after materializing planned worksheet session",
                "read-only same-sheet replacement should report materialized-session guard");
        }
    }
    check(replacement_failed,
        "read-only materialized session should block same-sheet replacement");
    const std::optional<std::string> replacement_error = editor.last_edit_error();
    check_readonly_state("after rejected replacement", replacement_error);
    check_public_inspection_preserves_last_edit_error(editor, replacement_error);

    bool rename_failed = false;
    try {
        editor.rename_sheet("Data", "BlockedReadonlyRename");
    } catch (const fastxlsx::FastXlsxError& error) {
        rename_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "read-only same-sheet rename should replace last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "read-only same-sheet rename diagnostic should match thrown error");
            check_contains(*last_error,
                "cannot rename sheet after materializing planned worksheet session",
                "read-only same-sheet rename should report materialized-session guard");
            check_not_contains(*last_error, "replace sheet data",
                "read-only same-sheet rename should replace the replacement diagnostic");
        }
    }
    check(rename_failed,
        "read-only materialized session should block same-sheet rename");
    const std::optional<std::string> rename_error = editor.last_edit_error();
    check_readonly_state("after rejected rename", rename_error);
    check_public_inspection_preserves_last_edit_error(editor, rename_error);

    editor.save_as(output);
    check(editor.last_edit_error() == rename_error,
        "read-only no-op save_as after rejected same-sheet operations should preserve diagnostic");
    check_readonly_state("after no-op save_as", rename_error);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "read-only no-op save_as after rejected same-sheet operations should copy source entries");
}

void test_public_worksheet_editor_saved_clean_session_blocks_same_sheet_patch_operations()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-mixing-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-mixing-first.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-mixing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("saved-clean-same-sheet-preflight"));
    editor.save_as(first_output);

    check(!sheet.has_pending_changes(),
        "saved-clean same-sheet mixing setup should leave the borrowed handle clean");
    check(editor.pending_change_count() == 1,
        "saved-clean same-sheet mixing setup should retain the materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "saved-clean same-sheet mixing setup should clear dirty materialized names");
    const auto first_output_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_output_entries.at("xl/worksheets/sheet1.xml"),
        "saved-clean-same-sheet-preflight",
        "saved-clean same-sheet mixing setup should persist the initial materialized edit");

    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    const std::size_t expected_pending_count = editor.pending_change_count();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        editor.pending_worksheet_edits();
    const std::size_t expected_cell_count = sheet.cell_count();
    const std::size_t expected_memory = sheet.estimated_memory_usage();

    const auto check_saved_clean_state = [&] (
        std::string_view stage, const std::optional<std::string>& expected_error) {
        const std::string prefix = std::string(stage);
        check(editor.last_edit_error() == expected_error,
            prefix + " should preserve last_edit_error");
        check(editor.pending_change_count() == expected_pending_count,
            prefix + " should preserve the saved materialized handoff count");
        check(editor.pending_materialized_worksheet_names().empty(),
            prefix + " should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            prefix + " should keep dirty materialized cell count clear");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            prefix + " should keep dirty materialized memory clear");
        check(workbook_editor_edit_summaries_equal(
                  editor.pending_worksheet_edits(), expected_summaries),
            prefix + " should preserve saved-clean edit summaries");
        check(editor.source_worksheet_names() == expected_source_names,
            prefix + " should preserve source worksheet names");
        check(editor.worksheet_names() == expected_planned_names,
            prefix + " should preserve planned worksheet names");
        check(workbook_editor_catalog_entries_equal(
                  editor.worksheet_catalog(), expected_catalog),
            prefix + " should preserve worksheet catalog");
        check(!sheet.has_pending_changes(),
            prefix + " should keep the borrowed handle clean");
        check(sheet.cell_count() == expected_cell_count,
            prefix + " should preserve saved sparse cell count");
        check(sheet.estimated_memory_usage() == expected_memory,
            prefix + " should preserve saved sparse memory");
        const fastxlsx::CellValue current = sheet.get_cell(1, 1);
        check(current.kind() == fastxlsx::CellValueKind::Text &&
                current.text_value() == "saved-clean-same-sheet-preflight",
            prefix + " should preserve the saved materialized value");
    };

    check_saved_clean_state("before rejected saved-clean operations", std::nullopt);

    bool replacement_failed = false;
    try {
        editor.replace_sheet_data("Data",
            {{fastxlsx::CellValue::text("blocked-saved-clean-replacement")}});
    } catch (const fastxlsx::FastXlsxError& error) {
        replacement_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "saved-clean same-sheet replacement should populate last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "saved-clean same-sheet replacement diagnostic should match thrown error");
            check_contains(*last_error,
                "cannot replace sheet data after materializing planned worksheet session",
                "saved-clean same-sheet replacement should report materialized-session guard");
        }
    }
    check(replacement_failed,
        "saved-clean materialized session should block same-sheet replacement");
    const std::optional<std::string> replacement_error = editor.last_edit_error();
    check_saved_clean_state("after rejected saved-clean replacement", replacement_error);
    check_public_inspection_preserves_last_edit_error(editor, replacement_error);

    bool rename_failed = false;
    try {
        editor.rename_sheet("Data", "BlockedSavedCleanRename");
    } catch (const fastxlsx::FastXlsxError& error) {
        rename_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "saved-clean same-sheet rename should replace last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "saved-clean same-sheet rename diagnostic should match thrown error");
            check_contains(*last_error,
                "cannot rename sheet after materializing planned worksheet session",
                "saved-clean same-sheet rename should report materialized-session guard");
            check_not_contains(*last_error, "replace sheet data",
                "saved-clean same-sheet rename should replace the replacement diagnostic");
        }
    }
    check(rename_failed,
        "saved-clean materialized session should block same-sheet rename");
    const std::optional<std::string> rename_error = editor.last_edit_error();
    check_saved_clean_state("after rejected saved-clean rename", rename_error);
    check_public_inspection_preserves_last_edit_error(editor, rename_error);

    editor.save_as(output);
    check(editor.last_edit_error() == rename_error,
        "save_as after rejected saved-clean same-sheet operations should preserve diagnostic");
    check_saved_clean_state("after saved-clean retry save_as", rename_error);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == first_output_entries,
        "save_as after rejected saved-clean same-sheet operations should match first output");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "blocked-saved-clean-replacement",
        "rejected saved-clean replacement should not leak into output");
    check_not_contains(output_entries.at("xl/workbook.xml"), "BlockedSavedCleanRename",
        "rejected saved-clean rename should not leak into workbook catalog");
}

void test_public_worksheet_editor_clean_sessions_allow_cross_sheet_patch_operations()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-cross-sheet-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-cross-sheet-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        const fastxlsx::CellValue source_value = data.get_cell(1, 1);
        check(source_value.kind() == fastxlsx::CellValueKind::Text &&
                source_value.text_value() == "placeholder-a1",
            "read-only cross-sheet setup should materialize Data from source");
        check(!data.has_pending_changes(),
            "read-only cross-sheet setup should keep Data clean");

        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();

        editor.rename_sheet("Untouched", "ReadonlyOtherName");
        editor.replace_sheet_data("ReadonlyOtherName",
            {{fastxlsx::CellValue::text("readonly-cross-sheet-replacement")}});

        check_public_single_sheet_cross_sheet_success_state(
            editor, data, data_cell_count, data_memory, 2, "Data",
            "read-only cross-sheet Patch operations");
        const fastxlsx::CellValue preserved_data = data.get_cell(1, 1);
        check(preserved_data.kind() == fastxlsx::CellValueKind::Text &&
                preserved_data.text_value() == "placeholder-a1",
            "read-only cross-sheet Patch operations should preserve Data value");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"), R"(name="ReadonlyOtherName")",
            "read-only cross-sheet rename should persist the other sheet name");
        check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
            "read-only cross-sheet output should preserve Data source cell");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "readonly-cross-sheet-replacement",
            "read-only cross-sheet replacement should not leak into Data");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"),
            "readonly-cross-sheet-replacement",
            "read-only cross-sheet replacement should persist on the other sheet");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-cross-sheet-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-cross-sheet-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-cross-sheet-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-cross-sheet-data"));
        editor.save_as(first_output);

        check(!data.has_pending_changes(),
            "saved-clean cross-sheet setup should leave Data clean after save");
        const std::size_t saved_pending_count = editor.pending_change_count();
        check(saved_pending_count == 1,
            "saved-clean cross-sheet setup should retain one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "saved-clean cross-sheet setup should clear dirty materialized names");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();

        editor.replace_sheet_data("Untouched",
            {{fastxlsx::CellValue::text("saved-clean-cross-sheet-replacement")}});
        editor.rename_sheet("Untouched", "SavedCleanOtherName");

        check_public_single_sheet_cross_sheet_success_state(
            editor, data, data_cell_count, data_memory, saved_pending_count + 2,
            "Data", "saved-clean cross-sheet Patch operations");
        const fastxlsx::CellValue preserved_data = data.get_cell(1, 1);
        check(preserved_data.kind() == fastxlsx::CellValueKind::Text &&
                preserved_data.text_value() == "saved-clean-cross-sheet-data",
            "saved-clean cross-sheet Patch operations should preserve Data saved value");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"), R"(name="SavedCleanOtherName")",
            "saved-clean cross-sheet rename should persist the other sheet name");
        check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "saved-clean-cross-sheet-data",
            "saved-clean cross-sheet output should preserve Data saved cell");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "saved-clean-cross-sheet-replacement",
            "saved-clean cross-sheet replacement should not leak into Data");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"),
            "saved-clean-cross-sheet-replacement",
            "saved-clean cross-sheet replacement should persist on the other sheet");
    }
}

void test_public_worksheet_editor_clean_same_sheet_patch_failures_replace_diagnostics()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-preflight-diagnostics-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-preflight-diagnostics-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        const fastxlsx::CellValue source_value = data.get_cell(1, 1);
        check(source_value.kind() == fastxlsx::CellValueKind::Text &&
                source_value.text_value() == "placeholder-a1",
            "read-only preflight diagnostic setup should materialize Data");

        const std::optional<std::string> replacement_error =
            check_public_same_sheet_rename_then_replacement_guard_sequence(
                editor, "Data", "ReadonlyRenameFirst", "readonly-replacement-second",
                "read-only same-sheet Patch diagnostics");

        check_public_failed_same_sheet_patch_readonly_clean_state(
            editor, data, replacement_error, "Data",
            "read-only failed same-sheet Patch diagnostics");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "read-only failed same-sheet Patch diagnostics should keep no-op output copy-original");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-preflight-diagnostics-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-preflight-diagnostics-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-preflight-diagnostics-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-preflight-diagnostics-data"));
        editor.save_as(first_output);
        const auto first_output_entries = fastxlsx::test::read_zip_entries(first_output);

        check(!data.has_pending_changes(),
            "saved-clean preflight diagnostic setup should leave Data clean");
        const std::size_t saved_pending_count = editor.pending_change_count();
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> saved_summaries =
            editor.pending_worksheet_edits();

        const std::optional<std::string> replacement_error =
            check_public_same_sheet_rename_then_replacement_guard_sequence(
                editor, "Data", "SavedCleanRenameFirst",
                "saved-clean-replacement-second",
                "saved-clean same-sheet Patch diagnostics");

        check_public_failed_same_sheet_patch_saved_clean_state(
            editor, data, saved_pending_count, saved_summaries, replacement_error,
            "Data", "saved-clean failed same-sheet Patch diagnostics");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == first_output_entries,
            "saved-clean failed same-sheet Patch diagnostics should keep retry output unchanged");
    }
}

void test_public_worksheet_editor_clean_same_sheet_failure_then_cross_sheet_success_clears_diagnostic()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-failure-success-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-failure-success-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        const fastxlsx::CellValue source_value = data.get_cell(1, 1);
        check(source_value.kind() == fastxlsx::CellValueKind::Text &&
                source_value.text_value() == "placeholder-a1",
            "read-only failure-success setup should materialize Data from source");
        check(!data.has_pending_changes(),
            "read-only failure-success setup should keep Data clean");

        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-blocked-before-success")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only same-sheet replacement before recovery");
        check(!editor.has_pending_changes(),
            "read-only failed same-sheet replacement should not queue public edits");

        editor.rename_sheet("Untouched", "ReadonlyRecoveredOther");

        check_public_single_sheet_cross_sheet_success_state(
            editor, data, data_cell_count, data_memory, 1, "Data",
            "read-only cross-sheet recovery");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "read-only recovery save_as should keep last_edit_error clear");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"),
            R"(name="ReadonlyRecoveredOther")",
            "read-only recovery rename should persist on the other sheet");
        check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
            "read-only recovery output should preserve Data source cell");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "readonly-blocked-before-success",
            "read-only rejected replacement should not leak into Data");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-success-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-success-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-success-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-failure-success-data"));
        editor.save_as(first_output);

        check(!data.has_pending_changes(),
            "saved-clean failure-success setup should leave Data clean");
        const std::size_t saved_pending_count = editor.pending_change_count();
        check(saved_pending_count == 1,
            "saved-clean failure-success setup should retain one materialized handoff");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanBlockedBeforeSuccess");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean same-sheet rename before recovery");

        editor.replace_sheet_data("Untouched",
            {{fastxlsx::CellValue::text("saved-clean-cross-sheet-after-failure")}});

        check_public_single_sheet_cross_sheet_success_state(
            editor, data, data_cell_count, data_memory, saved_pending_count + 1,
            "Data", "saved-clean cross-sheet recovery");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "saved-clean recovery save_as should keep last_edit_error clear");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"),
            R"(name="Data")",
            "saved-clean rejected rename should not change Data sheet name");
        check_not_contains(output_entries.at("xl/workbook.xml"),
            "SavedCleanBlockedBeforeSuccess",
            "saved-clean rejected rename should not leak into workbook catalog");
        check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "saved-clean-failure-success-data",
            "saved-clean recovery output should preserve Data saved cell");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"),
            "saved-clean-cross-sheet-after-failure",
            "saved-clean recovery replacement should persist on the other sheet");
    }
}

void test_public_worksheet_editor_clean_same_sheet_failure_then_worksheet_mutation_clears_diagnostic()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-failure-mutation-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-failure-mutation-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        const fastxlsx::CellValue source_value = data.get_cell(1, 1);
        check(source_value.kind() == fastxlsx::CellValueKind::Text &&
                source_value.text_value() == "placeholder-a1",
            "read-only failure-mutation setup should materialize Data from source");
        check(!data.has_pending_changes(),
            "read-only failure-mutation setup should keep Data clean");

        const std::size_t data_cell_count = data.cell_count();

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-blocked-before-mutation")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only same-sheet replacement before worksheet mutation");
        check(!editor.has_pending_changes(),
            "read-only failed same-sheet replacement should not queue public edits before mutation");

        data.set_cell(2, 2,
            fastxlsx::CellValue::text("readonly-mutation-after-failure"));

        check_public_single_sheet_mutation_state(
            editor, data, data_cell_count + 1, 0, "Data",
            "read-only worksheet mutation recovery");
        const fastxlsx::CellValue recovered_value = data.get_cell(2, 2);
        check(recovered_value.kind() == fastxlsx::CellValueKind::Text &&
                recovered_value.text_value() == "readonly-mutation-after-failure",
            "read-only worksheet mutation recovery should read back the recovered value");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "read-only mutation recovery save_as should keep last_edit_error clear");
        check(!data.has_pending_changes(),
            "read-only mutation recovery save_as should clean the borrowed handle");
        check(editor.pending_change_count() == 1,
            "read-only mutation recovery save_as should record one materialized handoff");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "readonly-mutation-after-failure",
            "read-only mutation recovery output should persist the successful worksheet mutation");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "readonly-blocked-before-mutation",
            "read-only rejected replacement should not leak into Data");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-mutation-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-mutation-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-mutation-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-mutation-recovery-data"));
        editor.save_as(first_output);

        check(!data.has_pending_changes(),
            "saved-clean failure-mutation setup should leave Data clean");
        const std::size_t saved_pending_count = editor.pending_change_count();
        check(saved_pending_count == 1,
            "saved-clean failure-mutation setup should retain one materialized handoff");
        const std::size_t data_cell_count = data.cell_count();

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanBlockedBeforeMutation");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean same-sheet rename before worksheet mutation");

        data.erase_cell(2, 1);

        check_public_single_sheet_mutation_state(
            editor, data, data_cell_count - 1, saved_pending_count, "Data",
            "saved-clean worksheet mutation recovery");
        check(!data.try_cell(2, 1).has_value(),
            "saved-clean worksheet mutation recovery should keep the erased cell absent");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "saved-clean mutation recovery save_as should keep last_edit_error clear");
        check(!data.has_pending_changes(),
            "saved-clean mutation recovery save_as should clean the borrowed handle");
        check(editor.pending_change_count() == saved_pending_count + 1,
            "saved-clean mutation recovery save_as should record one additional materialized handoff");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
            "saved-clean rejected rename should leave Data sheet name unchanged");
        check_not_contains(output_entries.at("xl/workbook.xml"),
            "SavedCleanBlockedBeforeMutation",
            "saved-clean rejected rename should not leak into workbook catalog");
        check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "saved-clean-mutation-recovery-data",
            "saved-clean mutation recovery output should preserve the saved Data value");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "placeholder-a2",
            "saved-clean mutation recovery output should persist the successful erase");
    }
}

void test_public_worksheet_editor_clean_same_sheet_failure_then_noop_erase_clears_diagnostic()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-failure-noop-erase-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-failure-noop-erase-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        const fastxlsx::CellValue source_value = data.get_cell(1, 1);
        check(source_value.kind() == fastxlsx::CellValueKind::Text &&
                source_value.text_value() == "placeholder-a1",
            "read-only no-op erase recovery setup should materialize Data from source");
        check(!data.has_pending_changes(),
            "read-only no-op erase recovery setup should keep Data clean");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        check(!data.try_cell(5, 5).has_value(),
            "read-only no-op erase recovery setup should use a missing target cell");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-blocked-before-noop-erase")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only same-sheet replacement failure before no-op erase");

        data.erase_cell(5, 5);

        check(!editor.last_edit_error().has_value(),
            "read-only successful no-op erase should clear prior same-sheet diagnostic");
        check(!data.has_pending_changes(),
            "read-only successful no-op erase should keep Data clean");
        check(!editor.has_pending_changes(),
            "read-only successful no-op erase should keep WorkbookEditor clean");
        check(editor.pending_change_count() == 0,
            "read-only successful no-op erase should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only successful no-op erase should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only successful no-op erase should keep dirty materialized cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "read-only successful no-op erase should keep dirty materialized memory empty");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "read-only successful no-op erase");
        check(!data.try_cell(5, 5).has_value(),
            "read-only successful no-op erase should keep the missing target absent");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "read-only no-op erase recovery save_as should keep last_edit_error clear");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "read-only no-op erase recovery output should remain copy-original");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "readonly-blocked-before-noop-erase",
            "read-only rejected replacement should not leak after no-op erase recovery");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-noop-erase-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-noop-erase-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-noop-erase-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-noop-erase-data"));
        editor.save_as(first_output);
        const auto first_output_entries =
            fastxlsx::test::read_zip_entries(first_output);

        check(!data.has_pending_changes(),
            "saved-clean no-op erase recovery setup should leave Data clean");
        const std::size_t saved_pending_count = editor.pending_change_count();
        check(saved_pending_count == 1,
            "saved-clean no-op erase recovery setup should retain one materialized handoff");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        check(!data.try_cell(5, 5).has_value(),
            "saved-clean no-op erase recovery setup should use a missing target cell");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanBlockedBeforeNoopErase");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean same-sheet rename failure before no-op erase");

        data.erase_cell(5, 5);

        check(!editor.last_edit_error().has_value(),
            "saved-clean successful no-op erase should clear prior same-sheet diagnostic");
        check(!data.has_pending_changes(),
            "saved-clean successful no-op erase should keep Data clean");
        check(editor.pending_change_count() == saved_pending_count,
            "saved-clean successful no-op erase should preserve saved handoff count");
        check(editor.pending_materialized_worksheet_names().empty(),
            "saved-clean successful no-op erase should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "saved-clean successful no-op erase should keep dirty materialized cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "saved-clean successful no-op erase should keep dirty materialized memory empty");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "saved-clean successful no-op erase");
        check(!data.try_cell(5, 5).has_value(),
            "saved-clean successful no-op erase should keep the missing target absent");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "saved-clean no-op erase recovery save_as should keep last_edit_error clear");
        check(!data.has_pending_changes(),
            "saved-clean no-op erase recovery save_as should leave Data clean");
        check(editor.pending_change_count() == saved_pending_count,
            "saved-clean no-op erase recovery save_as should not add a materialized handoff");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == first_output_entries,
            "saved-clean no-op erase recovery output should match the first saved output");
        check_not_contains(output_entries.at("xl/workbook.xml"),
            "SavedCleanBlockedBeforeNoopErase",
            "saved-clean rejected rename should not leak after no-op erase recovery");
    }
}

void test_public_worksheet_editor_noop_erase_recovery_preserves_same_sheet_patch_guard()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-noop-guard-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-noop-guard-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        const fastxlsx::CellValue source_value = data.get_cell(1, 1);
        check(source_value.kind() == fastxlsx::CellValueKind::Text &&
                source_value.text_value() == "placeholder-a1",
            "read-only no-op guard setup should materialize Data from source");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        check(!data.try_cell(5, 5).has_value(),
            "read-only no-op guard setup should use a missing erase target");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-blocked-before-noop-guard")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only initial replacement failure before no-op guard");

        data.erase_cell(5, 5);
        check(!editor.last_edit_error().has_value(),
            "read-only no-op erase should clear initial same-sheet diagnostic");
        check(!data.has_pending_changes(),
            "read-only no-op erase should keep Data clean before second same-sheet Patch");

        const std::optional<std::string> rename_error =
            check_public_same_sheet_guard_failure(
                editor,
                [&] {
                    editor.rename_sheet("Data", "ReadonlyNoopEraseBypass");
                },
                PublicMaterializedGuardDiagnostic::RenameSheet,
                "read-only same-sheet rename after no-op erase",
                PublicMaterializedGuardDiagnostic::ReplaceSheetData);

        check(!data.has_pending_changes(),
            "read-only second same-sheet failure should keep Data clean");
        check(!editor.has_pending_changes(),
            "read-only second same-sheet failure should keep WorkbookEditor clean");
        check(editor.pending_change_count() == 0,
            "read-only second same-sheet failure should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only second same-sheet failure should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only second same-sheet failure should keep dirty materialized cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "read-only second same-sheet failure should keep dirty materialized memory empty");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "read-only second same-sheet failure");
        check_public_inspection_preserves_last_edit_error(editor, rename_error);

        editor.save_as(output);
        check(editor.last_edit_error() == rename_error,
            "read-only no-op guard save_as should preserve the latest same-sheet diagnostic");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "read-only no-op guard output should remain copy-original");
        check_not_contains(output_entries.at("xl/workbook.xml"),
            "ReadonlyNoopEraseBypass",
            "read-only rejected rename after no-op erase should not leak into workbook catalog");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "readonly-blocked-before-noop-guard",
            "read-only rejected replacement before no-op erase should not leak into Data");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-noop-guard-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-noop-guard-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-noop-guard-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-noop-guard-data"));
        editor.save_as(first_output);
        const auto first_output_entries =
            fastxlsx::test::read_zip_entries(first_output);

        check(!data.has_pending_changes(),
            "saved-clean no-op guard setup should leave Data clean");
        const std::size_t saved_pending_count = editor.pending_change_count();
        check(saved_pending_count == 1,
            "saved-clean no-op guard setup should retain one materialized handoff");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        check(!data.try_cell(5, 5).has_value(),
            "saved-clean no-op guard setup should use a missing erase target");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanNoopEraseBypass");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean initial rename failure before no-op guard");

        data.erase_cell(5, 5);
        check(!editor.last_edit_error().has_value(),
            "saved-clean no-op erase should clear initial same-sheet diagnostic");
        check(!data.has_pending_changes(),
            "saved-clean no-op erase should keep Data clean before second same-sheet Patch");

        const std::optional<std::string> replacement_error =
            check_public_same_sheet_guard_failure(
                editor,
                [&] {
                    editor.replace_sheet_data("Data",
                        {{fastxlsx::CellValue::text("saved-clean-blocked-after-noop-guard")}});
                },
                PublicMaterializedGuardDiagnostic::ReplaceSheetData,
                "saved-clean same-sheet replacement after no-op erase",
                PublicMaterializedGuardDiagnostic::RenameSheet);

        check(!data.has_pending_changes(),
            "saved-clean second same-sheet failure should keep Data clean");
        check(editor.pending_change_count() == saved_pending_count,
            "saved-clean second same-sheet failure should preserve saved handoff count");
        check(editor.pending_materialized_worksheet_names().empty(),
            "saved-clean second same-sheet failure should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "saved-clean second same-sheet failure should keep dirty materialized cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "saved-clean second same-sheet failure should keep dirty materialized memory empty");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "saved-clean second same-sheet failure");
        check_public_inspection_preserves_last_edit_error(editor, replacement_error);

        editor.save_as(output);
        check(editor.last_edit_error() == replacement_error,
            "saved-clean no-op guard save_as should preserve the latest same-sheet diagnostic");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == first_output_entries,
            "saved-clean no-op guard output should match the first saved output");
        check_not_contains(output_entries.at("xl/workbook.xml"),
            "SavedCleanNoopEraseBypass",
            "saved-clean rejected rename before no-op erase should not leak into workbook catalog");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "saved-clean-blocked-after-noop-guard",
            "saved-clean rejected replacement after no-op erase should not leak into Data");
    }
}

void test_public_worksheet_editor_recovery_with_two_clean_handles_preserves_other_guard()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-recovery-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-recovery-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        const fastxlsx::CellValue data_value = data.get_cell(1, 1);
        const fastxlsx::CellValue untouched_value = untouched.get_cell(1, 1);
        check(data_value.kind() == fastxlsx::CellValueKind::Text &&
                data_value.text_value() == "placeholder-a1",
            "read-only two-clean recovery setup should materialize Data");
        check(untouched_value.kind() == fastxlsx::CellValueKind::Text &&
                untouched_value.text_value() == "keep-me",
            "read-only two-clean recovery setup should materialize Untouched");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        const std::size_t untouched_cell_count = untouched.cell_count();
        const std::size_t untouched_memory = untouched.estimated_memory_usage();
        check(!data.has_pending_changes() && !untouched.has_pending_changes(),
            "read-only two-clean recovery setup should keep both handles clean");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-two-clean-blocked-data")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only two-clean Data failure");

        data.erase_cell(5, 5);
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean Data no-op erase should clear Data diagnostic");
        check(!data.has_pending_changes(),
            "read-only two-clean Data no-op erase should keep Data clean");
        check(!untouched.has_pending_changes(),
            "read-only two-clean Data no-op erase should not dirty Untouched");

        const std::optional<std::string> untouched_error =
            check_public_same_sheet_guard_failure(
                editor,
                [&] {
                    editor.replace_sheet_data("Untouched",
                        {{fastxlsx::CellValue::text("readonly-two-clean-blocked-untouched")}});
                },
                PublicMaterializedGuardDiagnostic::ReplaceSheetData,
                "read-only two-clean Untouched failure");
        if (untouched_error.has_value()) {
            check_not_contains(*untouched_error, "Data",
                "read-only two-clean Untouched guard should not retain the Data diagnostic context");
        }

        check_public_two_clean_preserved_clean_handles_state(
            editor, data, untouched, data_cell_count, data_memory,
            untouched_cell_count, untouched_memory, 0, true,
            "read-only two-clean second failure");

        editor.save_as(output);
        check(editor.last_edit_error() == untouched_error,
            "read-only two-clean save_as should preserve the latest Untouched diagnostic");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_recovery_copy_original_output(
            output_entries, source_entries, "readonly-two-clean-blocked-data",
            "readonly-two-clean-blocked-untouched",
            "read-only two-clean output");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-recovery-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-recovery-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-recovery-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-handle-data"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-handle-untouched"));
        editor.save_as(first_output);

        const std::size_t saved_pending_count =
            check_public_two_clean_two_handle_clean_state(
                editor, data, untouched, 2,
                "saved-clean two-clean recovery setup");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        const std::size_t untouched_cell_count = untouched.cell_count();
        const std::size_t untouched_memory = untouched.estimated_memory_usage();

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanTwoHandleBlockedData");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean two-clean Data failure");

        data.set_cell(3, 3,
            fastxlsx::CellValue::text("saved-clean-two-handle-data-recovered"));
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean Data mutation should clear Data diagnostic");
        check_public_two_clean_single_dirty_materialized_state(
            editor, data, untouched, "Data", data_cell_count + 1,
            saved_pending_count, "saved-clean two-clean Data mutation");

        const std::optional<std::string> untouched_error =
            check_public_same_sheet_guard_failure(
                editor,
                [&] {
                    editor.replace_sheet_data("Untouched",
                        {{fastxlsx::CellValue::text("saved-clean-two-handle-blocked-untouched")}});
                },
                PublicMaterializedGuardDiagnostic::ReplaceSheetData,
                "saved-clean two-clean Untouched failure",
                PublicMaterializedGuardDiagnostic::RenameSheet);

        check_public_two_clean_single_dirty_materialized_state(
            editor, data, untouched, "Data", data_cell_count + 1,
            saved_pending_count, "saved-clean two-clean Untouched failure");
        check(data.cell_count() == data_cell_count + 1,
            "saved-clean two-clean Untouched failure should preserve Data added cell");
        check(data.estimated_memory_usage() >= data_memory,
            "saved-clean two-clean Untouched failure should preserve Data memory state");
        check_public_preserved_sheet_diagnostics(
            untouched, untouched_cell_count, untouched_memory, "Untouched",
            "saved-clean two-clean Untouched failure");
        check_public_inspection_preserves_last_edit_error(editor, untouched_error);

        editor.save_as(output);
        check(editor.last_edit_error() == untouched_error,
            "saved-clean two-clean save_as should preserve the latest Untouched diagnostic");
        check_public_two_clean_two_handle_clean_state(
            editor, data, untouched, saved_pending_count + 1,
            "saved-clean two-clean save_as");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_saved_clean_output(
            output_entries, "SavedCleanTwoHandleBlockedData",
            "saved-clean-two-handle-data-recovered",
            "saved-clean-two-handle-untouched",
            "saved-clean two-clean output",
            "saved-clean-two-handle-blocked-untouched");
    }
}

void test_public_worksheet_editor_recovery_with_two_clean_handles_allows_scoped_other_mutation()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-other-mutation-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-other-mutation-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        const fastxlsx::CellValue data_value = data.get_cell(1, 1);
        const fastxlsx::CellValue untouched_value = untouched.get_cell(1, 1);
        check(data_value.kind() == fastxlsx::CellValueKind::Text &&
                data_value.text_value() == "placeholder-a1",
            "read-only two-clean other-mutation setup should materialize Data");
        check(untouched_value.kind() == fastxlsx::CellValueKind::Text &&
                untouched_value.text_value() == "keep-me",
            "read-only two-clean other-mutation setup should materialize Untouched");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        const std::size_t untouched_cell_count = untouched.cell_count();
        check(!data.has_pending_changes() && !untouched.has_pending_changes(),
            "read-only two-clean other-mutation setup should keep both handles clean");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-two-clean-other-mutation-blocked-data")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only two-clean Data failure before other mutation");

        data.erase_cell(5, 5);
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean Data no-op recovery should clear diagnostic before other mutation");
        check(!data.has_pending_changes(),
            "read-only two-clean Data no-op recovery should keep Data clean");

        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("readonly-two-clean-untouched-mutated"));
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean Untouched mutation should keep last_edit_error clear");
        check_public_two_clean_single_dirty_materialized_state(
            editor, untouched, data, "Untouched", untouched_cell_count + 1, 0,
            "read-only two-clean Untouched mutation");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "read-only two-clean Untouched mutation");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean other-mutation save_as should keep last_edit_error clear");
        check_public_two_clean_two_handle_clean_state(
            editor, data, untouched, 1,
            "read-only two-clean other-mutation save_as");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_other_mutation_readonly_output(
            output_entries, source_entries, "readonly-two-clean-other-mutation-blocked-data",
            "readonly-two-clean-untouched-mutated",
            "read-only two-clean other-mutation output");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-other-mutation-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-other-mutation-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-other-mutation-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-handle-other-mutation-data"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-handle-other-mutation-untouched"));
        editor.save_as(first_output);

        const std::size_t saved_pending_count =
            check_public_two_clean_two_handle_clean_state(
                editor, data, untouched, 2,
                "saved-clean two-clean other-mutation setup");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t untouched_cell_count = untouched.cell_count();

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanTwoHandleOtherMutationBlockedData");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean two-clean Data failure before other mutation");

        data.set_cell(3, 3,
            fastxlsx::CellValue::text("saved-clean-two-handle-data-before-other-mutation"));
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean Data mutation should clear diagnostic before other mutation");
        check_public_two_clean_single_dirty_materialized_state(
            editor, data, untouched, "Data", data_cell_count + 1,
            saved_pending_count, "saved-clean two-clean Data mutation before other mutation");

        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("saved-clean-two-handle-untouched-after-data-recovery"));
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean Untouched mutation should keep last_edit_error clear");
        const std::size_t expected_dirty_cells =
            data_cell_count + 1 + untouched_cell_count + 1;
        const std::size_t expected_dirty_memory =
            data.estimated_memory_usage() + untouched.estimated_memory_usage();
        check_public_two_clean_both_dirty_materialized_state(
            editor, data, untouched, expected_dirty_cells, expected_dirty_memory,
            saved_pending_count, "saved-clean two-clean Untouched mutation");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean other-mutation save_as should keep last_edit_error clear");
        check_public_two_clean_two_handle_clean_state(
            editor, data, untouched, saved_pending_count + 2,
            "saved-clean two-clean other-mutation save_as");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_saved_clean_output(
            output_entries, "SavedCleanTwoHandleOtherMutationBlockedData",
            "saved-clean-two-handle-data-before-other-mutation",
            "saved-clean-two-handle-untouched-after-data-recovery",
            "saved-clean two-clean other-mutation output");
    }
}

void test_public_worksheet_editor_two_clean_recovery_failed_save_preserves_dirty_handles()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-failed-save-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-failed-save-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        (void)data.get_cell(1, 1);
        (void)untouched.get_cell(1, 1);

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-two-clean-failed-save-blocked-data")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only two-clean failed-save setup");

        data.erase_cell(5, 5);
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean failed-save no-op recovery should clear the diagnostic");
        data.set_cell(3, 3,
            fastxlsx::CellValue::text("readonly-two-clean-failed-save-data"));
        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("readonly-two-clean-failed-save-untouched"));

        const std::size_t expected_dirty_cells =
            data.cell_count() + untouched.cell_count();
        const std::size_t expected_dirty_memory =
            data.estimated_memory_usage() + untouched.estimated_memory_usage();
        check_public_two_clean_both_dirty_materialized_state(
            editor, data, untouched, expected_dirty_cells, expected_dirty_memory, 0,
            "read-only two-clean failed-save setup");

        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "read-only two-clean save_as over source should fail before dirty flush");
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean failed save_as should not create last_edit_error");
        check_public_two_clean_both_dirty_materialized_state(
            editor, data, untouched, expected_dirty_cells, expected_dirty_memory, 0,
            "read-only two-clean failed save_as");

        editor.save_as(output);
        check_public_two_clean_two_handle_clean_state(
            editor, data, untouched, 2,
            "read-only two-clean recovery safe save_as");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_failed_save_readonly_output(
            output_entries, "readonly-two-clean-failed-save-data",
            "readonly-two-clean-failed-save-untouched",
            "readonly-two-clean-failed-save-blocked-data",
            "read-only two-clean recovery safe output");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-failed-save-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-failed-save-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-failed-save-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-failed-save-data"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-failed-save-untouched"));
        editor.save_as(first_output);

        const std::size_t saved_pending_count =
            check_public_two_clean_two_handle_clean_state(
                editor, data, untouched, 2,
                "saved-clean two-clean failed-save setup");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanTwoCleanFailedSaveBlockedData");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean two-clean failed-save setup");

        data.set_cell(3, 3,
            fastxlsx::CellValue::text("saved-clean-two-clean-failed-save-data-recovered"));
        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("saved-clean-two-clean-failed-save-untouched-recovered"));
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean mutations should clear the same-sheet diagnostic");

        const std::size_t expected_dirty_cells =
            data.cell_count() + untouched.cell_count();
        const std::size_t expected_dirty_memory =
            data.estimated_memory_usage() + untouched.estimated_memory_usage();
        check_public_two_clean_both_dirty_materialized_state(
            editor, data, untouched, expected_dirty_cells, expected_dirty_memory,
            saved_pending_count, "saved-clean two-clean dirty handles");

        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "saved-clean two-clean save_as over source should fail before dirty flush");
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean failed save_as should not create last_edit_error");
        check_public_two_clean_both_dirty_materialized_state(
            editor, data, untouched, expected_dirty_cells, expected_dirty_memory,
            saved_pending_count, "saved-clean two-clean failed save_as");

        editor.save_as(output);
        check_public_two_clean_two_handle_clean_state(
            editor, data, untouched, saved_pending_count + 2,
            "saved-clean two-clean recovery safe save_as");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_saved_clean_output(
            output_entries, "SavedCleanTwoCleanFailedSaveBlockedData",
            "saved-clean-two-clean-failed-save-data-recovered",
            "saved-clean-two-clean-failed-save-untouched-recovered",
            "saved-clean two-clean recovery safe output");
    }
}

void test_public_worksheet_editor_two_clean_failed_save_retry_reacquire_preserves_sessions()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-reacquire-retry-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-reacquire-retry-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-reacquire-retry-second.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        (void)data.get_cell(1, 1);
        (void)untouched.get_cell(1, 1);

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text(
                        "readonly-two-clean-reacquire-retry-blocked-data")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only two-clean retry reacquire setup");

        data.erase_cell(5, 5);
        data.set_cell(3, 3,
            fastxlsx::CellValue::text("readonly-two-clean-reacquire-retry-data"));
        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("readonly-two-clean-reacquire-retry-untouched"));
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean retry reacquire mutations should clear diagnostic");

        check_public_two_clean_retry_failed_save_dirty_state(
            editor, data, untouched, 0,
            [&] { editor.save_as(source); },
            "read-only two-clean retry reacquire failed save");

        editor.save_as(first_output);
        check_public_two_clean_retry_two_handle_save_state(
            editor, data, untouched, 2,
            "read-only two-clean retry reacquire first safe save");

        fastxlsx::WorksheetEditor data_again = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched_again = editor.worksheet("Untouched");
        check_public_two_clean_retry_saved_value(
            data_again, 3, 3, "readonly-two-clean-reacquire-retry-data",
            "read-only retry post-save Data reacquire");
        check_public_two_clean_retry_saved_value(
            untouched_again, 2, 2, "readonly-two-clean-reacquire-retry-untouched",
            "read-only retry post-save Untouched reacquire");
        check_public_two_clean_retry_reacquire_clean_state(
            editor, data, untouched, data_again, untouched_again, 2,
            "read-only retry post-save reacquire");

        untouched_again.set_cell(4, 4,
            fastxlsx::CellValue::text("readonly-two-clean-reacquire-retry-second"));
        check_public_two_clean_retry_single_dirty_state(
            editor, untouched, untouched_again, data, data_again, "Untouched",
            untouched_again.cell_count(), untouched_again.estimated_memory_usage(),
            "read-only retry second mutation");

        editor.save_as(second_output);
        check_public_two_clean_retry_followup_save_state(
            editor, untouched, untouched_again, data, data_again, 3,
            "read-only retry second safe save");

        const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
        check_public_two_clean_retry_readonly_first_output(
            first_entries, "readonly-two-clean-reacquire-retry-data",
            "readonly-two-clean-reacquire-retry-untouched",
            "readonly-two-clean-reacquire-retry-blocked-data",
            "read-only retry first output");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_public_two_clean_retry_readonly_followup_output(
            second_entries, "readonly-two-clean-reacquire-retry-data",
            "readonly-two-clean-reacquire-retry-untouched",
            "xl/worksheets/sheet2.xml",
            "readonly-two-clean-reacquire-retry-second",
            "read-only retry second output");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-reacquire-retry-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-reacquire-retry-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-reacquire-retry-second.xlsx");
        const std::filesystem::path third_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-reacquire-retry-third.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-reacquire-retry-data-first"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-reacquire-retry-untouched-first"));
        editor.save_as(first_output);

        const std::size_t saved_pending_count =
            check_public_two_clean_retry_two_handle_save_state(
                editor, data, untouched, 2,
                "saved-clean two-clean retry reacquire setup");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanTwoCleanReacquireRetryBlockedData");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean two-clean retry reacquire setup");

        data.set_cell(3, 3,
            fastxlsx::CellValue::text("saved-clean-two-clean-reacquire-retry-data-recovered"));
        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("saved-clean-two-clean-reacquire-retry-untouched-recovered"));
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean retry reacquire mutations should clear diagnostic");

        check_public_two_clean_retry_failed_save_dirty_state(
            editor, data, untouched, saved_pending_count,
            [&] { editor.save_as(source); },
            "saved-clean two-clean retry reacquire failed save");

        editor.save_as(second_output);
        check_public_two_clean_retry_two_handle_save_state(
            editor, data, untouched, saved_pending_count + 2,
            "saved-clean two-clean retry reacquire second safe save");

        fastxlsx::WorksheetEditor data_again = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched_again = editor.worksheet("Untouched");
        check_public_two_clean_retry_saved_value(
            data_again, 1, 1, "saved-clean-two-clean-reacquire-retry-data-first",
            "saved-clean retry Data reacquire");
        check_public_two_clean_retry_saved_value(
            data_again, 3, 3, "saved-clean-two-clean-reacquire-retry-data-recovered",
            "saved-clean retry Data reacquire");
        check_public_two_clean_retry_saved_value(
            untouched_again, 1, 1,
            "saved-clean-two-clean-reacquire-retry-untouched-first",
            "saved-clean retry Untouched reacquire");
        check_public_two_clean_retry_saved_value(
            untouched_again, 2, 2,
            "saved-clean-two-clean-reacquire-retry-untouched-recovered",
            "saved-clean retry Untouched reacquire");
        check_public_two_clean_retry_reacquire_clean_state(
            editor, data, untouched, data_again, untouched_again,
            saved_pending_count + 2, "saved-clean retry post-save reacquire");

        data_again.set_cell(4, 4,
            fastxlsx::CellValue::text("saved-clean-two-clean-reacquire-retry-second"));
        check_public_two_clean_retry_single_dirty_state(
            editor, data, data_again, untouched, untouched_again, "Data",
            data_again.cell_count(), data_again.estimated_memory_usage(),
            "saved-clean retry second mutation");

        editor.save_as(third_output);
        check_public_two_clean_retry_followup_save_state(
            editor, data, data_again, untouched, untouched_again,
            saved_pending_count + 3, "saved-clean retry third safe save");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_public_two_clean_retry_saved_clean_recovery_output(
            second_entries, "SavedCleanTwoCleanReacquireRetryBlockedData",
            "saved-clean-two-clean-reacquire-retry-data-recovered",
            "saved-clean-two-clean-reacquire-retry-untouched-recovered",
            "saved-clean retry second output");

        const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
        check_public_two_clean_retry_saved_clean_followup_output(
            third_entries, "saved-clean-two-clean-reacquire-retry-data-first",
            "saved-clean-two-clean-reacquire-retry-data-recovered",
            "saved-clean-two-clean-reacquire-retry-untouched-recovered",
            "xl/worksheets/sheet1.xml",
            "saved-clean-two-clean-reacquire-retry-second",
            "saved-clean retry third output");
    }
}

void test_public_worksheet_editor_two_clean_failed_save_retry_queries_preserve_sessions()
{
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;
    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 9;

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-query-retry-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-query-retry-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-query-retry-second.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched", options);
        (void)data.get_cell(1, 1);
        (void)untouched.get_cell(1, 1);

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text(
                        "readonly-two-clean-query-retry-blocked-data")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only two-clean query retry setup");
        data.erase_cell(5, 5);
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("readonly-two-clean-query-retry-data"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("readonly-two-clean-query-retry-untouched"));

        check_public_two_clean_retry_failed_save_dirty_state(
            editor, data, untouched, 0,
            [&] { editor.save_as(source); },
            "read-only two-clean query retry failed save");
        editor.save_as(first_output);
        check_public_two_clean_retry_two_handle_save_state(
            editor, data, untouched, 2,
            "read-only two-clean query retry first safe save");

        fastxlsx::WorksheetEditor data_again = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched_again = editor.worksheet("Untouched", options);

        reject_public_two_clean_retry_query_failures(
            editor, options, mismatched_options, "read-only query retry");
        check_public_two_clean_retry_clean_after_query_failures(
            editor, data, untouched, data_again, untouched_again,
            expected_names, expected_catalog, 2, "read-only query retry");

        check_public_two_clean_retry_saved_value(
            data_again, 1, 1, "readonly-two-clean-query-retry-data",
            "read-only query retry Data");
        check_public_two_clean_retry_saved_value(
            untouched_again, 1, 1, "readonly-two-clean-query-retry-untouched",
            "read-only query retry Untouched");

        untouched_again.set_cell(2, 2,
            fastxlsx::CellValue::text("readonly-two-clean-query-retry-second"));
        check_public_two_clean_retry_single_dirty_state(
            editor, untouched, untouched_again, data, data_again, "Untouched",
            untouched_again.cell_count(), untouched_again.estimated_memory_usage(),
            "read-only query retry post-query mutation");

        editor.save_as(second_output);
        check_public_two_clean_retry_followup_save_state(
            editor, untouched, untouched_again, data, data_again, 3,
            "read-only query retry second safe save");

        const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
        check_public_two_clean_retry_readonly_first_output(
            first_entries, "readonly-two-clean-query-retry-data",
            "readonly-two-clean-query-retry-untouched",
            "readonly-two-clean-query-retry-blocked-data",
            "read-only query retry first output");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_public_two_clean_retry_readonly_followup_output(
            second_entries, "readonly-two-clean-query-retry-data",
            "readonly-two-clean-query-retry-untouched",
            "xl/worksheets/sheet2.xml", "readonly-two-clean-query-retry-second",
            "read-only query retry second output");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-query-retry-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-query-retry-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-query-retry-second.xlsx");
        const std::filesystem::path third_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-query-retry-third.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched", options);
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-query-retry-data-first"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-query-retry-untouched-first"));
        editor.save_as(first_output);
        const std::size_t saved_pending_count =
            check_public_two_clean_retry_two_handle_save_state(
                editor, data, untouched, 2,
                "saved-clean two-clean query retry setup");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanTwoCleanQueryRetryBlockedData");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean two-clean query retry setup");
        data.set_cell(3, 3,
            fastxlsx::CellValue::text("saved-clean-two-clean-query-retry-data-recovered"));
        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("saved-clean-two-clean-query-retry-untouched-recovered"));
        check(!editor.last_edit_error().has_value(),
            "saved-clean query retry recovery mutations should clear diagnostic");

        check_public_two_clean_retry_failed_save_dirty_state(
            editor, data, untouched, saved_pending_count,
            [&] { editor.save_as(source); },
            "saved-clean two-clean query retry failed save");
        editor.save_as(second_output);
        check_public_two_clean_retry_two_handle_save_state(
            editor, data, untouched, saved_pending_count + 2,
            "saved-clean two-clean query retry second safe save");

        fastxlsx::WorksheetEditor data_again = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched_again = editor.worksheet("Untouched", options);

        reject_public_two_clean_retry_query_failures(
            editor, options, mismatched_options, "saved-clean query retry");
        check_public_two_clean_retry_clean_after_query_failures(
            editor, data, untouched, data_again, untouched_again,
            expected_names, expected_catalog, saved_pending_count + 2,
            "saved-clean query retry");

        check_public_two_clean_retry_saved_value(
            data_again, 1, 1, "saved-clean-two-clean-query-retry-data-first",
            "saved-clean query retry Data");
        check_public_two_clean_retry_saved_value(
            data_again, 3, 3, "saved-clean-two-clean-query-retry-data-recovered",
            "saved-clean query retry Data");
        check_public_two_clean_retry_saved_value(
            untouched_again, 1, 1,
            "saved-clean-two-clean-query-retry-untouched-first",
            "saved-clean query retry Untouched");
        check_public_two_clean_retry_saved_value(
            untouched_again, 2, 2,
            "saved-clean-two-clean-query-retry-untouched-recovered",
            "saved-clean query retry Untouched");

        data_again.set_cell(4, 4,
            fastxlsx::CellValue::text("saved-clean-two-clean-query-retry-second"));
        check_public_two_clean_retry_single_dirty_state(
            editor, data, data_again, untouched, untouched_again, "Data",
            data_again.cell_count(), data_again.estimated_memory_usage(),
            "saved-clean query retry post-query mutation");

        editor.save_as(third_output);
        check_public_two_clean_retry_followup_save_state(
            editor, data, data_again, untouched, untouched_again,
            saved_pending_count + 3, "saved-clean query retry third safe save");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_public_two_clean_retry_saved_clean_recovery_output(
            second_entries, "SavedCleanTwoCleanQueryRetryBlockedData",
            "saved-clean-two-clean-query-retry-data-recovered",
            "saved-clean-two-clean-query-retry-untouched-recovered",
            "saved-clean query retry second output");

        const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
        check_public_two_clean_retry_saved_clean_followup_output(
            third_entries, "saved-clean-two-clean-query-retry-data-first",
            "saved-clean-two-clean-query-retry-data-recovered",
            "saved-clean-two-clean-query-retry-untouched-recovered",
            "xl/worksheets/sheet1.xml", "saved-clean-two-clean-query-retry-second",
            "saved-clean query retry third output");
    }
}

void test_public_worksheet_editor_two_clean_failed_save_retry_invalid_reads_preserve_sessions()
{
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-invalid-read-retry-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-invalid-read-retry-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-invalid-read-retry-second.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched", options);
        (void)data.get_cell(1, 1);
        (void)untouched.get_cell(1, 1);

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text(
                        "readonly-two-clean-invalid-read-retry-blocked-data")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only two-clean invalid-read retry setup");
        data.erase_cell(5, 5);
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("readonly-two-clean-invalid-read-retry-data"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("readonly-two-clean-invalid-read-retry-untouched"));

        check_public_two_clean_retry_failed_save_dirty_state(
            editor, data, untouched, 0,
            [&] { editor.save_as(source); },
            "read-only two-clean invalid-read retry failed save");
        editor.save_as(first_output);
        check_public_two_clean_retry_two_handle_save_state(
            editor, data, untouched, 2,
            "read-only two-clean invalid-read retry first safe save");

        fastxlsx::WorksheetEditor data_again = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched_again = editor.worksheet("Untouched", options);
        const std::size_t data_count = data_again.cell_count();
        const std::size_t data_memory = data_again.estimated_memory_usage();
        const std::size_t untouched_count = untouched_again.cell_count();
        const std::size_t untouched_memory = untouched_again.estimated_memory_usage();

        reject_public_two_clean_retry_invalid_reads(
            data, untouched, data_again, untouched_again, "read-only retry");
        check_public_two_clean_retry_clean_after_invalid_reads(
            editor, data, untouched, data_again, untouched_again,
            expected_names, expected_catalog, 2, data_count, data_memory,
            untouched_count, untouched_memory, "read-only retry");

        check_public_two_clean_retry_saved_value(
            data_again, 1, 1, "readonly-two-clean-invalid-read-retry-data",
            "read-only retry invalid reads Data");
        check_public_two_clean_retry_saved_value(
            untouched_again, 1, 1,
            "readonly-two-clean-invalid-read-retry-untouched",
            "read-only retry invalid reads Untouched");

        data_again.set_cell(4, 4,
            fastxlsx::CellValue::text("readonly-two-clean-invalid-read-retry-second"));
        check_public_two_clean_retry_single_dirty_state(
            editor, data, data_again, untouched, untouched_again, "Data",
            data_again.cell_count(), data_again.estimated_memory_usage(),
            "read-only retry post-invalid-read mutation");

        editor.save_as(second_output);
        check_public_two_clean_retry_followup_save_state(
            editor, data, data_again, untouched, untouched_again, 3,
            "read-only retry invalid-read second safe save");

        const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
        check_public_two_clean_retry_readonly_first_output(
            first_entries, "readonly-two-clean-invalid-read-retry-data",
            "readonly-two-clean-invalid-read-retry-untouched",
            "readonly-two-clean-invalid-read-retry-blocked-data",
            "read-only retry invalid-read first output");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_public_two_clean_retry_readonly_followup_output(
            second_entries, "readonly-two-clean-invalid-read-retry-data",
            "readonly-two-clean-invalid-read-retry-untouched",
            "xl/worksheets/sheet1.xml",
            "readonly-two-clean-invalid-read-retry-second",
            "read-only retry invalid-read second output");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-invalid-read-retry-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-invalid-read-retry-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-invalid-read-retry-second.xlsx");
        const std::filesystem::path third_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-invalid-read-retry-third.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched", options);
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-read-retry-data-first"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-read-retry-untouched-first"));
        editor.save_as(first_output);
        const std::size_t saved_pending_count =
            check_public_two_clean_retry_two_handle_save_state(
                editor, data, untouched, 2,
                "saved-clean two-clean invalid-read retry setup");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanTwoCleanInvalidReadRetryBlockedData");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean two-clean invalid-read retry setup");
        data.set_cell(3, 3,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-read-retry-data-recovered"));
        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-read-retry-untouched-recovered"));

        check_public_two_clean_retry_failed_save_dirty_state(
            editor, data, untouched, saved_pending_count,
            [&] { editor.save_as(source); },
            "saved-clean two-clean invalid-read retry failed save");
        editor.save_as(second_output);
        check_public_two_clean_retry_two_handle_save_state(
            editor, data, untouched, saved_pending_count + 2,
            "saved-clean two-clean invalid-read retry second safe save");

        fastxlsx::WorksheetEditor data_again = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched_again = editor.worksheet("Untouched", options);
        const std::size_t data_count = data_again.cell_count();
        const std::size_t data_memory = data_again.estimated_memory_usage();
        const std::size_t untouched_count = untouched_again.cell_count();
        const std::size_t untouched_memory = untouched_again.estimated_memory_usage();

        reject_public_two_clean_retry_invalid_reads(
            data, untouched, data_again, untouched_again, "saved-clean retry");
        check_public_two_clean_retry_clean_after_invalid_reads(
            editor, data, untouched, data_again, untouched_again,
            expected_names, expected_catalog, saved_pending_count + 2,
            data_count, data_memory, untouched_count, untouched_memory,
            "saved-clean retry");

        check_public_two_clean_retry_saved_value(
            data_again, 1, 1,
            "saved-clean-two-clean-invalid-read-retry-data-first",
            "saved-clean retry invalid reads Data");
        check_public_two_clean_retry_saved_value(
            data_again, 3, 3,
            "saved-clean-two-clean-invalid-read-retry-data-recovered",
            "saved-clean retry invalid reads Data");
        check_public_two_clean_retry_saved_value(
            untouched_again, 1, 1,
            "saved-clean-two-clean-invalid-read-retry-untouched-first",
            "saved-clean retry invalid reads Untouched");
        check_public_two_clean_retry_saved_value(
            untouched_again, 2, 2,
            "saved-clean-two-clean-invalid-read-retry-untouched-recovered",
            "saved-clean retry invalid reads Untouched");

        untouched_again.set_cell(4, 4,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-read-retry-second"));
        check_public_two_clean_retry_single_dirty_state(
            editor, untouched, untouched_again, data, data_again, "Untouched",
            untouched_again.cell_count(), untouched_again.estimated_memory_usage(),
            "saved-clean retry post-invalid-read mutation");

        editor.save_as(third_output);
        check_public_two_clean_retry_followup_save_state(
            editor, untouched, untouched_again, data, data_again,
            saved_pending_count + 3, "saved-clean retry invalid-read third safe save");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_public_two_clean_retry_saved_clean_recovery_output(
            second_entries, "SavedCleanTwoCleanInvalidReadRetryBlockedData",
            "saved-clean-two-clean-invalid-read-retry-data-recovered",
            "saved-clean-two-clean-invalid-read-retry-untouched-recovered",
            "saved-clean retry invalid-read second output");

        const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
        check_public_two_clean_retry_saved_clean_followup_output(
            third_entries, "saved-clean-two-clean-invalid-read-retry-data-first",
            "saved-clean-two-clean-invalid-read-retry-data-recovered",
            "saved-clean-two-clean-invalid-read-retry-untouched-recovered",
            "xl/worksheets/sheet2.xml",
            "saved-clean-two-clean-invalid-read-retry-second",
            "saved-clean retry invalid-read third output");
    }
}

void test_public_worksheet_editor_two_clean_failed_save_retry_invalid_mutations_preserve_sessions()
{
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-invalid-mutation-retry-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-invalid-mutation-retry-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-invalid-mutation-retry-second.xlsx");
        constexpr std::string_view rejected_prefix =
            "readonly-two-clean-rejected-invalid-mutation";

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched", options);
        (void)data.get_cell(1, 1);
        (void)untouched.get_cell(1, 1);

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text(
                        "readonly-two-clean-invalid-mutation-retry-blocked-data")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only two-clean invalid-mutation retry setup");
        data.erase_cell(5, 5);
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("readonly-two-clean-invalid-mutation-retry-data"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("readonly-two-clean-invalid-mutation-retry-untouched"));

        check_public_two_clean_retry_failed_save_dirty_state(
            editor, data, untouched, 0,
            [&] { editor.save_as(source); },
            "read-only two-clean invalid-mutation retry failed save_as");
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean invalid-mutation retry failed save_as should not create last_edit_error");
        editor.save_as(first_output);
        check_public_two_clean_retry_two_handle_save_state(
            editor, data, untouched, 2,
            "read-only two-clean invalid-mutation retry first safe save");

        fastxlsx::WorksheetEditor data_again = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched_again = editor.worksheet("Untouched", options);
        const std::size_t data_count = data_again.cell_count();
        const std::size_t data_memory = data_again.estimated_memory_usage();
        const std::size_t untouched_count = untouched_again.cell_count();
        const std::size_t untouched_memory = untouched_again.estimated_memory_usage();

        reject_public_two_clean_retry_invalid_mutations(
            data, untouched, data_again, untouched_again, rejected_prefix,
            "read-only retry invalid mutations");
        check_public_two_clean_retry_clean_after_invalid_mutations(
            editor, data, untouched, data_again, untouched_again,
            expected_names, expected_catalog, 2, data_count, data_memory,
            untouched_count, untouched_memory,
            "read-only retry invalid mutations");
        const std::optional<std::string> read_only_invalid_mutation_error =
            editor.last_edit_error();

        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "read-only retry failed save_as after invalid mutations should reject source overwrite");
        check(editor.last_edit_error() == read_only_invalid_mutation_error,
            "read-only retry failed save_as after invalid mutations should preserve diagnostics");
        check_public_two_clean_retry_clean_after_invalid_mutations(
            editor, data, untouched, data_again, untouched_again,
            expected_names, expected_catalog, 2, data_count, data_memory,
            untouched_count, untouched_memory,
            "read-only retry failed save_as after invalid mutations");

        check_public_two_clean_retry_saved_value(
            data_again, 1, 1,
            "readonly-two-clean-invalid-mutation-retry-data",
            "read-only retry invalid mutations Data");
        check_public_two_clean_retry_saved_value(
            untouched_again, 1, 1,
            "readonly-two-clean-invalid-mutation-retry-untouched",
            "read-only retry invalid mutations Untouched");

        data_again.set_cell(4, 4,
            fastxlsx::CellValue::text("readonly-two-clean-invalid-mutation-retry-second"));
        check(!editor.last_edit_error().has_value(),
            "read-only retry valid post-invalid-mutation edit should clear diagnostics");
        check_public_two_clean_retry_single_dirty_state(
            editor, data, data_again, untouched, untouched_again, "Data",
            data_again.cell_count(), data_again.estimated_memory_usage(),
            "read-only retry post-invalid-mutation edit");

        editor.save_as(second_output);
        check_public_two_clean_retry_followup_save_state(
            editor, data, data_again, untouched, untouched_again, 3,
            "read-only retry invalid-mutation second safe save");

        const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
        check_public_two_clean_retry_readonly_first_output(
            first_entries, "readonly-two-clean-invalid-mutation-retry-data",
            "readonly-two-clean-invalid-mutation-retry-untouched",
            "readonly-two-clean-invalid-mutation-retry-blocked-data",
            "read-only retry invalid-mutation first output", rejected_prefix);

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_public_two_clean_retry_readonly_followup_output(
            second_entries, "readonly-two-clean-invalid-mutation-retry-data",
            "readonly-two-clean-invalid-mutation-retry-untouched",
            "xl/worksheets/sheet1.xml",
            "readonly-two-clean-invalid-mutation-retry-second",
            "read-only retry invalid-mutation second output", rejected_prefix);
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-invalid-mutation-retry-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-invalid-mutation-retry-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-invalid-mutation-retry-second.xlsx");
        const std::filesystem::path third_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-invalid-mutation-retry-third.xlsx");
        constexpr std::string_view rejected_prefix =
            "saved-clean-two-clean-rejected-invalid-mutation";

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched", options);
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-mutation-retry-data-first"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-mutation-retry-untouched-first"));
        editor.save_as(first_output);
        const std::size_t saved_pending_count =
            check_public_two_clean_retry_two_handle_save_state(
                editor, data, untouched, 2,
                "saved-clean two-clean invalid-mutation retry setup");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanTwoCleanInvalidMutationRetryBlockedData");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean two-clean invalid-mutation retry setup");
        data.set_cell(3, 3,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-mutation-retry-data-recovered"));
        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-mutation-retry-untouched-recovered"));

        check_public_two_clean_retry_failed_save_dirty_state(
            editor, data, untouched, saved_pending_count,
            [&] { editor.save_as(source); },
            "saved-clean two-clean invalid-mutation retry failed save_as");
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean invalid-mutation retry failed save_as should not create last_edit_error");
        editor.save_as(second_output);
        check_public_two_clean_retry_two_handle_save_state(
            editor, data, untouched, saved_pending_count + 2,
            "saved-clean two-clean invalid-mutation retry second safe save");

        fastxlsx::WorksheetEditor data_again = editor.worksheet("Data", options);
        fastxlsx::WorksheetEditor untouched_again = editor.worksheet("Untouched", options);
        const std::size_t data_count = data_again.cell_count();
        const std::size_t data_memory = data_again.estimated_memory_usage();
        const std::size_t untouched_count = untouched_again.cell_count();
        const std::size_t untouched_memory = untouched_again.estimated_memory_usage();

        reject_public_two_clean_retry_invalid_mutations(
            data, untouched, data_again, untouched_again, rejected_prefix,
            "saved-clean retry invalid mutations");
        check_public_two_clean_retry_clean_after_invalid_mutations(
            editor, data, untouched, data_again, untouched_again,
            expected_names, expected_catalog, saved_pending_count + 2,
            data_count, data_memory, untouched_count, untouched_memory,
            "saved-clean retry invalid mutations");
        const std::optional<std::string> saved_clean_invalid_mutation_error =
            editor.last_edit_error();

        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "saved-clean retry failed save_as after invalid mutations should reject source overwrite");
        check(editor.last_edit_error() == saved_clean_invalid_mutation_error,
            "saved-clean retry failed save_as after invalid mutations should preserve diagnostics");
        check_public_two_clean_retry_clean_after_invalid_mutations(
            editor, data, untouched, data_again, untouched_again,
            expected_names, expected_catalog, saved_pending_count + 2,
            data_count, data_memory, untouched_count, untouched_memory,
            "saved-clean retry failed save_as after invalid mutations");

        check_public_two_clean_retry_saved_value(
            data_again, 1, 1,
            "saved-clean-two-clean-invalid-mutation-retry-data-first",
            "saved-clean retry invalid mutations Data");
        check_public_two_clean_retry_saved_value(
            data_again, 3, 3,
            "saved-clean-two-clean-invalid-mutation-retry-data-recovered",
            "saved-clean retry invalid mutations Data");
        check_public_two_clean_retry_saved_value(
            untouched_again, 1, 1,
            "saved-clean-two-clean-invalid-mutation-retry-untouched-first",
            "saved-clean retry invalid mutations Untouched");
        check_public_two_clean_retry_saved_value(
            untouched_again, 2, 2,
            "saved-clean-two-clean-invalid-mutation-retry-untouched-recovered",
            "saved-clean retry invalid mutations Untouched");

        untouched_again.set_cell(4, 4,
            fastxlsx::CellValue::text("saved-clean-two-clean-invalid-mutation-retry-second"));
        check(!editor.last_edit_error().has_value(),
            "saved-clean retry valid post-invalid-mutation edit should clear diagnostics");
        check_public_two_clean_retry_single_dirty_state(
            editor, untouched, untouched_again, data, data_again, "Untouched",
            untouched_again.cell_count(), untouched_again.estimated_memory_usage(),
            "saved-clean retry post-invalid-mutation edit");

        editor.save_as(third_output);
        check_public_two_clean_retry_followup_save_state(
            editor, untouched, untouched_again, data, data_again,
            saved_pending_count + 3, "saved-clean retry invalid-mutation third safe save");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_public_two_clean_retry_saved_clean_recovery_output(
            second_entries, "SavedCleanTwoCleanInvalidMutationRetryBlockedData",
            "saved-clean-two-clean-invalid-mutation-retry-data-recovered",
            "saved-clean-two-clean-invalid-mutation-retry-untouched-recovered",
            "saved-clean retry invalid-mutation second output", rejected_prefix);

        const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
        check_public_two_clean_retry_saved_clean_followup_output(
            third_entries, "saved-clean-two-clean-invalid-mutation-retry-data-first",
            "saved-clean-two-clean-invalid-mutation-retry-data-recovered",
            "saved-clean-two-clean-invalid-mutation-retry-untouched-recovered",
            "xl/worksheets/sheet2.xml",
            "saved-clean-two-clean-invalid-mutation-retry-second",
            "saved-clean retry invalid-mutation third output", rejected_prefix);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = workbook_editor_shard_from_args(argc, argv);
        std::printf("fastxlsx.workbook_editor shard: %.*s\n",
            static_cast<int>(shard.size()), shard.data());

        if (should_run_workbook_editor_shard(shard, "public")) {
            test_public_worksheet_editor_handles_invalidate_after_owner_move();
            test_public_worksheet_editor_invalidated_handle_failures_preserve_owner_diagnostics();
            test_public_worksheet_editor_handles_invalidate_after_move_assignment();
            test_public_worksheet_editor_move_assignment_invalidated_handle_failures_preserve_owner_diagnostics();
            test_public_worksheet_editor_saved_clean_handle_invalidated_after_owner_move_preserves_state();
            test_public_worksheet_editor_saved_clean_handles_invalidated_after_move_assignment_preserve_source_state();
            test_public_worksheet_editor_readonly_handle_invalidated_after_owner_move_preserves_clean_state();
            test_public_worksheet_editor_readonly_handles_invalidated_after_move_assignment_preserve_source_state();
            test_public_worksheet_editor_set_cell_auto_flushes_on_save_as();
            test_public_try_worksheet_missing_returns_empty_and_preserves_diagnostics();
            test_public_worksheet_missing_throws_and_preserves_diagnostics();
            test_public_try_worksheet_existing_handle_reads_mutates_and_saves();
            test_public_worksheet_editor_normalizes_explicit_default_style_id();
            test_public_worksheet_editor_rejects_non_default_style_id_without_mutation();
            test_public_worksheet_editor_rejects_non_default_style_id_a1_without_mutation();
            test_public_try_worksheet_reuses_options_and_blocks_replacement_mix();
            test_public_worksheet_editor_reacquire_reuses_dirty_session();
            test_public_worksheet_editor_reacquire_after_save_reuses_session();
            test_public_worksheet_editor_post_save_reacquire_preserves_clean_diagnostics();
            test_public_worksheet_editor_post_save_option_mismatch_preserves_session();
            test_public_worksheet_editor_post_save_summary_tracks_reacquire_dirty_state();
            test_public_worksheet_editor_post_save_summary_preserves_rename_context();
            test_public_worksheet_editor_failed_save_as_preserves_renamed_summary_dirty_state();
            test_public_worksheet_editor_renamed_materialized_diagnostics_follow_planned_name();
            test_public_worksheet_editor_rename_back_materialized_diagnostics_use_source_name();
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
            test_public_worksheet_editor_row_column_overloads_reject_invalid_coordinates();
            test_public_worksheet_editor_invalid_cell_reads_preserve_prior_diagnostic();
            test_public_worksheet_editor_sparse_cells_snapshot();
            test_public_worksheet_editor_sparse_cells_range_snapshot();
            test_public_worksheet_editor_sparse_cells_invalid_range_preserves_prior_diagnostic();
            test_public_worksheet_editor_erase_cell_auto_flushes_on_save_as();
            test_public_worksheet_editor_options_guard_failure_preserves_state();
            test_public_worksheet_editor_memory_budget_guard_failure_preserves_state();
            test_public_worksheet_editor_mutation_memory_budget_failure_preserves_state();
            test_public_worksheet_editor_mutation_max_cells_failure_preserves_state();
            test_public_worksheet_editor_erase_releases_guardrail_budget_for_insertions();
            test_public_worksheet_editor_missing_erase_after_guardrail_failure_stays_clean();
            test_public_worksheet_editor_blank_insertions_obey_guardrail_budgets();
            test_public_worksheet_editor_last_edit_error_replaces_failed_mutation_diagnostics();
            test_public_workbook_editor_last_edit_error_replaces_mixed_edit_diagnostics();
            test_public_worksheet_editor_blocks_same_sheet_patch_operations();
            test_public_worksheet_editor_readonly_session_blocks_same_sheet_patch_operations();
            test_public_worksheet_editor_saved_clean_session_blocks_same_sheet_patch_operations();
            test_public_worksheet_editor_clean_sessions_allow_cross_sheet_patch_operations();
            test_public_worksheet_editor_clean_same_sheet_patch_failures_replace_diagnostics();
            test_public_worksheet_editor_clean_same_sheet_failure_then_cross_sheet_success_clears_diagnostic();
            test_public_worksheet_editor_clean_same_sheet_failure_then_worksheet_mutation_clears_diagnostic();
            test_public_worksheet_editor_clean_same_sheet_failure_then_noop_erase_clears_diagnostic();
            test_public_worksheet_editor_noop_erase_recovery_preserves_same_sheet_patch_guard();
            test_public_worksheet_editor_recovery_with_two_clean_handles_preserves_other_guard();
            test_public_worksheet_editor_recovery_with_two_clean_handles_allows_scoped_other_mutation();
            test_public_worksheet_editor_two_clean_recovery_failed_save_preserves_dirty_handles();
            test_public_worksheet_editor_two_clean_failed_save_retry_reacquire_preserves_sessions();
            test_public_worksheet_editor_two_clean_failed_save_retry_queries_preserve_sessions();
            test_public_worksheet_editor_two_clean_failed_save_retry_invalid_reads_preserve_sessions();
            test_public_worksheet_editor_two_clean_failed_save_retry_invalid_mutations_preserve_sessions();
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
