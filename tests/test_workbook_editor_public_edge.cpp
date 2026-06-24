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
    return shard == "all" || shard == "public-edge";
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
void test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMaxCoordinate");
    editor.rename_sheet("TransientMaxCoordinate", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-max-coordinate-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before max-coordinate recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before max-coordinate edits should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired max-coordinate handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };
    check(!editor.last_edit_error().has_value(),
        "max-coordinate setup should start without edit diagnostics");

    constexpr std::uint32_t max_row = 1048576;
    constexpr std::uint32_t max_column = 16384;
    reacquired.set_cell(max_row, max_column,
        fastxlsx::CellValue::text("max-coordinate"));

    check(!editor.last_edit_error().has_value(),
        "post-recovery max-coordinate mutation should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "post-recovery max-coordinate mutation should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "post-recovery max-coordinate mutation should keep A1, B1, A2, and XFD1048576");
    {
        const fastxlsx::CellValue by_position =
            sheet.get_cell(max_row, max_column);
        const fastxlsx::CellValue by_a1 = reacquired.get_cell("XFD1048576");
        const fastxlsx::CellValue preserved_b1 = sheet.get_cell("B1");
        const fastxlsx::CellValue preserved_a2 = reacquired.get_cell(2, 1);
        check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                by_position.text_value() == "max-coordinate",
            "post-recovery row/column read should expose the max-coordinate edit");
        check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                by_a1.text_value() == "max-coordinate",
            "post-recovery A1 read should expose the max-coordinate edit");
        check(preserved_b1.kind() == fastxlsx::CellValueKind::Number &&
                preserved_b1.number_value() == 1.0,
            "post-recovery max-coordinate mutation should preserve source-backed B1");
        check(preserved_a2.kind() == fastxlsx::CellValueKind::Text &&
                preserved_a2.text_value() == "placeholder-a2",
            "post-recovery max-coordinate mutation should preserve source-backed A2");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            reacquired.sparse_cells(
                fastxlsx::CellRange {max_row, max_column, max_row, max_column});
        check(edge_cells.size() == 1,
            "max-coordinate sparse range should return exactly the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == max_row &&
                    edge_cells[0].reference.column == max_column,
                "max-coordinate sparse range should preserve the legal Excel boundary");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "max-coordinate",
                "max-coordinate sparse range should copy the edge text value");
        }
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxCoordinate",
        "post-recovery max-coordinate mutation",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean max-coordinate recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the max-coordinate materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear max-coordinate dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear max-coordinate dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear max-coordinate dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after max-coordinate edit");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after max-coordinate edit");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "source package should not contain the later max-coordinate edit");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first max-coordinate recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMaxCoordinate",
        "first max-coordinate recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-max-coordinate-first",
        "first output should contain the saved value before max-coordinate edit");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "first output should not contain the later max-coordinate edit");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second max-coordinate recovery output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientMaxCoordinate",
        "second max-coordinate recovery output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "second max-coordinate output should refresh dimension through XFD1048576");
    check_contains(second_worksheet_xml,
        R"(<row r="1048576"><c r="XFD1048576" t="inlineStr"><is><t>max-coordinate</t></is></c></row>)",
        "second output should persist the sparse max-coordinate row without dense materialization");
    check_contains(second_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "second output should preserve source-backed B1 number");
    check_contains(second_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "second output should preserve source-backed A2 text");
    check_contains(second_worksheet_xml, "rename-back-max-coordinate-first",
        "second output should preserve the prior setup text alongside the sparse max-coordinate edit");
    check_not_contains(second_worksheet_xml, "placeholder-a1",
        "second output should not reload stale source A1 after setup replacement");
}

void test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_erase_shrinks_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-erase-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-erase-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-erase-second.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-erase-third.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMaxCoordinateErase");
    editor.rename_sheet("TransientMaxCoordinateErase", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-max-coordinate-erase-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before max-coordinate erase setup flushes");
    editor.save_as(first_output);

    constexpr std::uint32_t max_row = 1048576;
    constexpr std::uint32_t max_column = 16384;
    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    reacquired.set_cell(max_row, max_column,
        fastxlsx::CellValue::text("max-coordinate-to-erase"));
    editor.save_as(second_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean max-coordinate erase setup handles");
    check(sheet.get_cell("XFD1048576").text_value() == "max-coordinate-to-erase",
        "saved max-coordinate setup should be visible through the original handle");
    check(reacquired.get_cell(max_row, max_column).text_value() == "max-coordinate-to-erase",
        "saved max-coordinate setup should be visible through the reacquired handle");

    fastxlsx::WorksheetEditor after_max_save = editor.worksheet("Data", options);
    check(!after_max_save.has_pending_changes(),
        "matching reacquire after max-coordinate save should start clean");
    check(after_max_save.get_cell("XFD1048576").text_value() == "max-coordinate-to-erase",
        "matching reacquire after max-coordinate save should reuse the saved edge state");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    after_max_save.erase_cell(max_row, max_column);
    check(!editor.last_edit_error().has_value(),
        "erasing the saved max-coordinate cell should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            after_max_save.has_pending_changes(),
        "erasing the saved max-coordinate cell should dirty shared handles");
    check(sheet.cell_count() == 3 && reacquired.cell_count() == 3 &&
            after_max_save.cell_count() == 3,
        "erasing the saved max-coordinate cell should shrink the sparse record count");
    check(!after_max_save.try_cell("XFD1048576").has_value(),
        "erasing the saved max-coordinate cell should remove the edge record");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell(max_row, max_column);
    }), "get_cell should throw after erasing the saved max-coordinate record");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(
                fastxlsx::CellRange {max_row, max_column, max_row, max_column});
        check(edge_cells.empty(),
            "max-coordinate sparse range should be empty after erasing the edge record");
    }
    check(reacquired.estimated_memory_usage() == sheet.estimated_memory_usage(),
        "max-coordinate erase should keep reacquired handle memory aligned");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        after_max_save,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxCoordinateErase",
        "post-recovery max-coordinate erase",
        4,
        3,
        sheet.estimated_memory_usage());

    editor.save_as(third_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !after_max_save.has_pending_changes(),
        "third safe save_as should clean max-coordinate erase handles");
    check(editor.pending_change_count() == 5,
        "third safe save_as should count the max-coordinate erase materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "third safe save_as should clear max-coordinate erase dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "third safe save_as should clear max-coordinate erase dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "third safe save_as should clear summaries after max-coordinate erase");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "max-coordinate-to-erase",
        "source package should not contain the transient max-coordinate erase value");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "second output should contain the saved max-coordinate cell before erase");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "max-coordinate-to-erase",
        "second output should contain the max-coordinate value before erase");

    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    const std::string third_workbook_xml = third_entries.at("xl/workbook.xml");
    const std::string third_worksheet_xml = third_entries.at("xl/worksheets/sheet1.xml");
    check_contains(third_workbook_xml, R"(name="Data")",
        "third max-coordinate erase output should keep the restored source name");
    check_not_contains(third_workbook_xml, "TransientMaxCoordinateErase",
        "third max-coordinate erase output should not leak the transient planned name");
    check_contains(third_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "third output should shrink dimension after erasing the max-coordinate cell");
    check_not_contains(third_worksheet_xml, "XFD1048576",
        "third output should omit the erased max-coordinate cell reference");
    check_not_contains(third_worksheet_xml, "max-coordinate-to-erase",
        "third output should omit the erased max-coordinate cell value");
    check_contains(third_worksheet_xml, "rename-back-max-coordinate-erase-first",
        "third output should preserve the setup A1 text after edge erase");
    check_contains(third_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "third output should preserve source-backed B1 number after edge erase");
    check_contains(third_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "third output should preserve source-backed A2 text after edge erase");
}

void test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_a1_mutations()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-a1-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-a1-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-a1-second.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-a1-third.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMaxCoordinateA1");
    editor.rename_sheet("TransientMaxCoordinateA1", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-max-coordinate-a1-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before max-coordinate A1 setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before max-coordinate A1 mutations should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired max-coordinate A1 handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    reacquired.set_cell("XFD1048576",
        fastxlsx::CellValue::text("max-coordinate-a1"));
    check(!editor.last_edit_error().has_value(),
        "max-coordinate A1 set_cell should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "max-coordinate A1 set_cell should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "max-coordinate A1 set_cell should add one sparse edge record");
    {
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        const fastxlsx::CellValue by_position = reacquired.get_cell(1048576, 16384);
        check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                by_a1.text_value() == "max-coordinate-a1",
            "max-coordinate A1 set_cell should read back through the A1 overload");
        check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                by_position.text_value() == "max-coordinate-a1",
            "max-coordinate A1 set_cell should read back through row/column overloads");
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxCoordinateA1",
        "post-recovery max-coordinate A1 set",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean max-coordinate A1 set handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the max-coordinate A1 set materialized handoff");

    fastxlsx::WorksheetEditor after_set_save = editor.worksheet("Data", options);
    check(!after_set_save.has_pending_changes(),
        "matching reacquire after max-coordinate A1 set save should start clean");
    check(after_set_save.get_cell("XFD1048576").text_value() == "max-coordinate-a1",
        "matching reacquire after max-coordinate A1 set save should reuse the saved edge state");

    after_set_save.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "max-coordinate A1 erase_cell should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            after_set_save.has_pending_changes(),
        "max-coordinate A1 erase_cell should dirty shared handles");
    check(sheet.cell_count() == 3 && reacquired.cell_count() == 3 &&
            after_set_save.cell_count() == 3,
        "max-coordinate A1 erase_cell should shrink the sparse record count");
    check(!after_set_save.try_cell("XFD1048576").has_value(),
        "max-coordinate A1 erase_cell should remove the edge record");
    check(threw_fastxlsx_error([&] {
        (void)reacquired.get_cell("XFD1048576");
    }), "A1 get_cell should throw after max-coordinate A1 erase_cell");
    check(reacquired.estimated_memory_usage() == sheet.estimated_memory_usage(),
        "max-coordinate A1 erase should keep reacquired handle memory aligned");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        after_set_save,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxCoordinateA1",
        "post-recovery max-coordinate A1 erase",
        4,
        3,
        sheet.estimated_memory_usage());

    editor.save_as(third_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !after_set_save.has_pending_changes(),
        "third safe save_as should clean max-coordinate A1 erase handles");
    check(editor.pending_change_count() == 5,
        "third safe save_as should count the max-coordinate A1 erase materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "third safe save_as should clear max-coordinate A1 dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "third safe save_as should clear max-coordinate A1 dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "third safe save_as should clear max-coordinate A1 summaries");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "max-coordinate-a1",
        "source package should not contain max-coordinate A1 mutations");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first max-coordinate A1 output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMaxCoordinateA1",
        "first max-coordinate A1 output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-max-coordinate-a1-first",
        "first output should contain the setup value before max-coordinate A1 mutations");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "first output should not contain the later max-coordinate A1 mutation");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second max-coordinate A1 output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientMaxCoordinateA1",
        "second max-coordinate A1 output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "second max-coordinate A1 output should refresh dimension through XFD1048576");
    check_contains(second_worksheet_xml,
        R"(<row r="1048576"><c r="XFD1048576" t="inlineStr"><is><t>max-coordinate-a1</t></is></c></row>)",
        "second output should persist the max-coordinate A1 set_cell payload");

    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    const std::string third_workbook_xml = third_entries.at("xl/workbook.xml");
    const std::string third_worksheet_xml = third_entries.at("xl/worksheets/sheet1.xml");
    check_contains(third_workbook_xml, R"(name="Data")",
        "third max-coordinate A1 output should keep the restored source name");
    check_not_contains(third_workbook_xml, "TransientMaxCoordinateA1",
        "third max-coordinate A1 output should not leak the transient planned name");
    check_contains(third_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "third max-coordinate A1 output should shrink dimension after A1 erase_cell");
    check_not_contains(third_worksheet_xml, "XFD1048576",
        "third output should omit the A1-erased max-coordinate reference");
    check_not_contains(third_worksheet_xml, R"(<t>max-coordinate-a1</t>)",
        "third output should omit the A1-erased max-coordinate cell value");
    check_contains(third_worksheet_xml, "rename-back-max-coordinate-a1-first",
        "third output should preserve the setup A1 text after A1 edge erase");
    check_contains(third_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "third output should preserve source-backed B1 after A1 edge erase");
    check_contains(third_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "third output should preserve source-backed A2 after A1 edge erase");
}

void test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_blank_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-blank-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-blank-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-blank-second.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-blank-third.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMaxCoordinateBlank");
    editor.rename_sheet("TransientMaxCoordinateBlank", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-max-coordinate-blank-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before max-coordinate blank setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before max-coordinate blank should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired max-coordinate blank handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    reacquired.set_cell("XFD1048576", fastxlsx::CellValue::blank());
    check(!editor.last_edit_error().has_value(),
        "max-coordinate blank set_cell should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "max-coordinate blank set_cell should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "max-coordinate blank set_cell should add one explicit blank edge record");
    {
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        const fastxlsx::CellValue by_position = reacquired.get_cell(1048576, 16384);
        check(by_a1.kind() == fastxlsx::CellValueKind::Blank,
            "max-coordinate blank should read back through the A1 overload");
        check(by_position.kind() == fastxlsx::CellValueKind::Blank,
            "max-coordinate blank should read back through row/column overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "max-coordinate blank sparse range should return exactly the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "max-coordinate blank sparse range should preserve the legal Excel boundary");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Blank,
                "max-coordinate blank sparse range should preserve the explicit blank value");
        }
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxCoordinateBlank",
        "post-recovery max-coordinate blank set",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean max-coordinate blank handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the max-coordinate blank materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear max-coordinate blank dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear max-coordinate blank dirty cell count");

    fastxlsx::WorksheetEditor after_blank_save = editor.worksheet("Data", options);
    check(!after_blank_save.has_pending_changes(),
        "matching reacquire after max-coordinate blank save should start clean");
    check(after_blank_save.get_cell("XFD1048576").kind() == fastxlsx::CellValueKind::Blank,
        "matching reacquire after max-coordinate blank save should preserve the blank edge state");

    after_blank_save.erase_cell(1048576, 16384);
    check(!editor.last_edit_error().has_value(),
        "row/column erase after max-coordinate blank save should not create diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            after_blank_save.has_pending_changes(),
        "erasing the max-coordinate blank should dirty shared handles");
    check(sheet.cell_count() == 3 && reacquired.cell_count() == 3 &&
            after_blank_save.cell_count() == 3,
        "erasing the max-coordinate blank should shrink the sparse record count");
    check(!after_blank_save.try_cell("XFD1048576").has_value(),
        "erasing the max-coordinate blank should remove the edge record");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            reacquired.sparse_cells(
                fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "max-coordinate blank sparse range should be empty after erase");
    }
    check(after_blank_save.estimated_memory_usage() == sheet.estimated_memory_usage(),
        "max-coordinate blank erase should keep reacquired handle memory aligned");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        after_blank_save,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxCoordinateBlank",
        "post-recovery max-coordinate blank erase",
        4,
        3,
        sheet.estimated_memory_usage());

    editor.save_as(third_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !after_blank_save.has_pending_changes(),
        "third safe save_as should clean max-coordinate blank erase handles");
    check(editor.pending_change_count() == 5,
        "third safe save_as should count the max-coordinate blank erase materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "third safe save_as should clear max-coordinate blank dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "third safe save_as should clear max-coordinate blank dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "third safe save_as should clear max-coordinate blank summaries");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "source package should not contain max-coordinate blank mutations");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first max-coordinate blank output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMaxCoordinateBlank",
        "first max-coordinate blank output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-max-coordinate-blank-first",
        "first output should contain the setup value before max-coordinate blank");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "first output should not contain the later max-coordinate blank record");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second max-coordinate blank output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientMaxCoordinateBlank",
        "second max-coordinate blank output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "second max-coordinate blank output should refresh dimension through XFD1048576");
    check_contains(second_worksheet_xml,
        R"(<row r="1048576"><c r="XFD1048576"/></row>)",
        "second output should persist the explicit blank max-coordinate cell as an empty c element");
    check_not_contains(second_worksheet_xml, R"(r="XFD1048576" t="inlineStr")",
        "second output should not serialize the explicit blank as inline text");

    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    const std::string third_workbook_xml = third_entries.at("xl/workbook.xml");
    const std::string third_worksheet_xml = third_entries.at("xl/worksheets/sheet1.xml");
    check_contains(third_workbook_xml, R"(name="Data")",
        "third max-coordinate blank output should keep the restored source name");
    check_not_contains(third_workbook_xml, "TransientMaxCoordinateBlank",
        "third max-coordinate blank output should not leak the transient planned name");
    check_contains(third_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "third max-coordinate blank output should shrink dimension after erase");
    check_not_contains(third_worksheet_xml, "XFD1048576",
        "third output should omit the erased max-coordinate blank reference");
    check_contains(third_worksheet_xml, "rename-back-max-coordinate-blank-first",
        "third output should preserve the setup A1 text after blank edge erase");
    check_contains(third_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "third output should preserve source-backed B1 after blank edge erase");
    check_contains(third_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "third output should preserve source-backed A2 after blank edge erase");
}

void test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_formula_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-formula-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-formula-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-formula-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMaxCoordinateFormula");
    editor.rename_sheet("TransientMaxCoordinateFormula", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-max-coordinate-formula-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before max-coordinate formula setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before max-coordinate formula should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired max-coordinate formula handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    reacquired.set_cell(1048576, 16384,
        fastxlsx::CellValue::formula(R"(SUM(A1:B1)&"<edge>")"));
    check(!editor.last_edit_error().has_value(),
        "max-coordinate formula set_cell should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "max-coordinate formula set_cell should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "max-coordinate formula set_cell should add one sparse edge record");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = reacquired.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Formula &&
                by_position.text_value() == R"(SUM(A1:B1)&"<edge>")",
            "max-coordinate formula should read back through row/column overloads");
        check(by_a1.kind() == fastxlsx::CellValueKind::Formula &&
                by_a1.text_value() == R"(SUM(A1:B1)&"<edge>")",
            "max-coordinate formula should read back through the A1 overload");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "max-coordinate formula sparse range should return exactly the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "max-coordinate formula sparse range should preserve the legal Excel boundary");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    edge_cells[0].value.text_value() == R"(SUM(A1:B1)&"<edge>")",
                "max-coordinate formula sparse range should preserve formula text");
        }
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxCoordinateFormula",
        "post-recovery max-coordinate formula set",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean max-coordinate formula handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the max-coordinate formula materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear max-coordinate formula dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear max-coordinate formula dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear max-coordinate formula summaries");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "source package should not contain max-coordinate formula mutations");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "SUM(A1:B1)",
        "source package should not contain max-coordinate formula text");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first max-coordinate formula output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMaxCoordinateFormula",
        "first max-coordinate formula output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-max-coordinate-formula-first",
        "first output should contain the setup value before max-coordinate formula");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "first output should not contain the later max-coordinate formula record");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second max-coordinate formula output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientMaxCoordinateFormula",
        "second max-coordinate formula output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "second max-coordinate formula output should refresh dimension through XFD1048576");
    check_contains(second_worksheet_xml,
        R"(<row r="1048576"><c r="XFD1048576"><f>SUM(A1:B1)&amp;"&lt;edge&gt;"</f></c></row>)",
        "second output should persist escaped formula at the sparse max-coordinate row");
    check_not_contains(second_worksheet_xml,
        R"(<c r="XFD1048576"><f>SUM(A1:B1)&amp;"&lt;edge&gt;"</f><v>)",
        "second output should not generate a cached value for the max-coordinate formula");
    check_contains(second_worksheet_xml, "rename-back-max-coordinate-formula-first",
        "second output should preserve the setup A1 text alongside the max-coordinate formula");
    check_contains(second_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "second output should preserve source-backed B1 after max-coordinate formula");
    check_contains(second_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "second output should preserve source-backed A2 after max-coordinate formula");
}

void test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_scalar_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-scalar-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-scalar-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-scalar-second.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-scalar-third.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMaxCoordinateScalar");
    editor.rename_sheet("TransientMaxCoordinateScalar", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-max-coordinate-scalar-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before max-coordinate scalar setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before max-coordinate scalar should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired max-coordinate scalar handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    reacquired.set_cell(1048576, 16384, fastxlsx::CellValue::number(42.5));
    check(!editor.last_edit_error().has_value(),
        "max-coordinate number set_cell should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "max-coordinate number set_cell should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "max-coordinate number set_cell should add one sparse edge record");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = reacquired.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Number &&
                by_position.number_value() == 42.5,
            "max-coordinate number should read back through row/column overloads");
        check(by_a1.kind() == fastxlsx::CellValueKind::Number &&
                by_a1.number_value() == 42.5,
            "max-coordinate number should read back through the A1 overload");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "max-coordinate number sparse range should return exactly the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "max-coordinate number sparse range should preserve the legal Excel boundary");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    edge_cells[0].value.number_value() == 42.5,
                "max-coordinate number sparse range should preserve scalar value");
        }
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxCoordinateScalar",
        "post-recovery max-coordinate number set",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean max-coordinate number handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the max-coordinate number materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear max-coordinate number dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear max-coordinate number dirty cell count");

    fastxlsx::WorksheetEditor after_number_save = editor.worksheet("Data", options);
    check(!after_number_save.has_pending_changes(),
        "matching reacquire after max-coordinate number save should start clean");
    check(after_number_save.get_cell("XFD1048576").kind() == fastxlsx::CellValueKind::Number &&
            after_number_save.get_cell("XFD1048576").number_value() == 42.5,
        "matching reacquire after max-coordinate number save should preserve the edge number");

    after_number_save.set_cell("XFD1048576", fastxlsx::CellValue::boolean(false));
    check(!editor.last_edit_error().has_value(),
        "max-coordinate boolean set_cell should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            after_number_save.has_pending_changes(),
        "max-coordinate boolean set_cell should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4 &&
            after_number_save.cell_count() == 4,
        "max-coordinate boolean set_cell should keep one sparse edge record");
    {
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        const fastxlsx::CellValue by_position =
            after_number_save.get_cell(1048576, 16384);
        check(by_a1.kind() == fastxlsx::CellValueKind::Boolean &&
                !by_a1.boolean_value(),
            "max-coordinate boolean should read back through the A1 overload");
        check(by_position.kind() == fastxlsx::CellValueKind::Boolean &&
                !by_position.boolean_value(),
            "max-coordinate boolean should read back through row/column overloads");
    }
    check(after_number_save.estimated_memory_usage() == sheet.estimated_memory_usage(),
        "max-coordinate boolean overwrite should keep reacquired handle memory aligned");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        after_number_save,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxCoordinateScalar",
        "post-recovery max-coordinate boolean set",
        4,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(third_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !after_number_save.has_pending_changes(),
        "third safe save_as should clean max-coordinate boolean handles");
    check(editor.pending_change_count() == 5,
        "third safe save_as should count the max-coordinate boolean materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "third safe save_as should clear max-coordinate boolean dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "third safe save_as should clear max-coordinate boolean dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "third safe save_as should clear max-coordinate boolean summaries");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "source package should not contain max-coordinate scalar mutations");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first max-coordinate scalar output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMaxCoordinateScalar",
        "first max-coordinate scalar output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-max-coordinate-scalar-first",
        "first output should contain the setup value before max-coordinate scalar edits");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "first output should not contain the later max-coordinate scalar record");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second max-coordinate scalar output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientMaxCoordinateScalar",
        "second max-coordinate scalar output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "second max-coordinate number output should refresh dimension through XFD1048576");
    check_contains(second_worksheet_xml,
        R"(<row r="1048576"><c r="XFD1048576"><v>42.5</v></c></row>)",
        "second output should persist scalar number at the sparse max-coordinate row");
    check_not_contains(second_worksheet_xml, R"(r="XFD1048576" t="b")",
        "second output should not serialize the number as a boolean");

    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    const std::string third_workbook_xml = third_entries.at("xl/workbook.xml");
    const std::string third_worksheet_xml = third_entries.at("xl/worksheets/sheet1.xml");
    check_contains(third_workbook_xml, R"(name="Data")",
        "third max-coordinate scalar output should keep the restored source name");
    check_not_contains(third_workbook_xml, "TransientMaxCoordinateScalar",
        "third max-coordinate scalar output should not leak the transient planned name");
    check_contains(third_worksheet_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "third max-coordinate boolean output should keep dimension through XFD1048576");
    check_contains(third_worksheet_xml,
        R"(<row r="1048576"><c r="XFD1048576" t="b"><v>0</v></c></row>)",
        "third output should persist scalar boolean false at the sparse max-coordinate row");
    check_not_contains(third_worksheet_xml, R"(<c r="XFD1048576"><v>42.5</v></c>)",
        "third output should remove the previous max-coordinate number payload");
    check_contains(third_worksheet_xml, "rename-back-max-coordinate-scalar-first",
        "third output should preserve the setup A1 text alongside the max-coordinate boolean");
    check_contains(third_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "third output should preserve source-backed B1 after max-coordinate scalar edits");
    check_contains(third_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "third output should preserve source-backed A2 after max-coordinate scalar edits");
}

void test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_scalar_erase_shrinks_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-scalar-erase-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-scalar-erase-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-scalar-erase-second.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-scalar-erase-third.xlsx");
    const std::filesystem::path fourth_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-scalar-erase-fourth.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMaxScalarErase");
    editor.rename_sheet("TransientMaxScalarErase", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-max-coordinate-scalar-erase-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before max-coordinate scalar erase setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before max-coordinate scalar erase should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired max-coordinate scalar erase handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    reacquired.set_cell(1048576, 16384, fastxlsx::CellValue::number(42.5));
    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean max-coordinate scalar erase number handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the max-coordinate scalar erase number handoff");

    fastxlsx::WorksheetEditor after_number_save = editor.worksheet("Data", options);
    check(!after_number_save.has_pending_changes(),
        "matching reacquire after max-coordinate scalar erase number save should start clean");
    check(after_number_save.get_cell("XFD1048576").kind() == fastxlsx::CellValueKind::Number &&
            after_number_save.get_cell("XFD1048576").number_value() == 42.5,
        "matching reacquire after max-coordinate scalar erase number save should preserve the edge number");

    after_number_save.set_cell("XFD1048576", fastxlsx::CellValue::boolean(false));
    editor.save_as(third_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !after_number_save.has_pending_changes(),
        "third safe save_as should clean max-coordinate scalar erase boolean handles");
    check(editor.pending_change_count() == 5,
        "third safe save_as should count the max-coordinate scalar erase boolean handoff");

    fastxlsx::WorksheetEditor after_boolean_save = editor.worksheet("Data", options);
    check(!after_boolean_save.has_pending_changes(),
        "matching reacquire after max-coordinate scalar erase boolean save should start clean");
    {
        const fastxlsx::CellValue saved_boolean = after_boolean_save.get_cell(1048576, 16384);
        check(saved_boolean.kind() == fastxlsx::CellValueKind::Boolean &&
                !saved_boolean.boolean_value(),
            "matching reacquire after max-coordinate scalar erase boolean save should preserve the edge boolean");
    }

    after_boolean_save.erase_cell(1048576, 16384);
    check(!editor.last_edit_error().has_value(),
        "row/column erase after max-coordinate scalar save should not create diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            after_number_save.has_pending_changes() && after_boolean_save.has_pending_changes(),
        "erasing the saved max-coordinate scalar should dirty shared handles");
    check(sheet.cell_count() == 3 && reacquired.cell_count() == 3 &&
            after_number_save.cell_count() == 3 && after_boolean_save.cell_count() == 3,
        "erasing the saved max-coordinate scalar should shrink the sparse record count");
    check(!after_boolean_save.try_cell("XFD1048576").has_value(),
        "erasing the saved max-coordinate scalar should remove the edge record");
    check(threw_fastxlsx_error([&] {
        (void)after_number_save.get_cell(1048576, 16384);
    }), "get_cell should throw after erasing the saved max-coordinate scalar");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "max-coordinate scalar sparse range should be empty after erase");
    }
    check(after_boolean_save.estimated_memory_usage() == sheet.estimated_memory_usage(),
        "max-coordinate scalar erase should keep reacquired handle memory aligned");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        after_boolean_save,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientMaxScalarErase",
        "post-recovery max-coordinate scalar erase",
        5,
        3,
        sheet.estimated_memory_usage());

    editor.save_as(fourth_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !after_number_save.has_pending_changes() && !after_boolean_save.has_pending_changes(),
        "fourth safe save_as should clean max-coordinate scalar erase handles");
    check(editor.pending_change_count() == 6,
        "fourth safe save_as should count the max-coordinate scalar erase handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "fourth safe save_as should clear max-coordinate scalar erase dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "fourth safe save_as should clear max-coordinate scalar erase dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "fourth safe save_as should clear max-coordinate scalar erase summaries");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "source package should not contain max-coordinate scalar erase mutations");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first max-coordinate scalar erase output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMaxScalarErase",
        "first max-coordinate scalar erase output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-max-coordinate-scalar-erase-first",
        "first output should contain the setup value before max-coordinate scalar erase");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "first output should not contain the later max-coordinate scalar erase record");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"), R"(<dimension ref="A1:XFD1048576"/>)",
        "second max-coordinate scalar erase output should refresh dimension through XFD1048576");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        R"(<row r="1048576"><c r="XFD1048576"><v>42.5</v></c></row>)",
        "second output should contain the saved max-coordinate number before scalar erase");

    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    check_contains(third_entries.at("xl/worksheets/sheet1.xml"), R"(<dimension ref="A1:XFD1048576"/>)",
        "third max-coordinate scalar erase output should keep dimension through XFD1048576 before erase");
    check_contains(third_entries.at("xl/worksheets/sheet1.xml"),
        R"(<row r="1048576"><c r="XFD1048576" t="b"><v>0</v></c></row>)",
        "third output should contain the saved max-coordinate boolean before scalar erase");

    const auto fourth_entries = fastxlsx::test::read_zip_entries(fourth_output);
    const std::string fourth_workbook_xml = fourth_entries.at("xl/workbook.xml");
    const std::string fourth_worksheet_xml = fourth_entries.at("xl/worksheets/sheet1.xml");
    check_contains(fourth_workbook_xml, R"(name="Data")",
        "fourth max-coordinate scalar erase output should keep the restored source name");
    check_not_contains(fourth_workbook_xml, "TransientMaxScalarErase",
        "fourth max-coordinate scalar erase output should not leak the transient planned name");
    check_contains(fourth_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "fourth output should shrink dimension after erasing the max-coordinate scalar");
    check_not_contains(fourth_worksheet_xml, "XFD1048576",
        "fourth output should omit the erased max-coordinate scalar reference");
    check_not_contains(fourth_worksheet_xml, "42.5",
        "fourth output should omit the erased max-coordinate number payload");
    check_not_contains(fourth_worksheet_xml, R"(t="b"><v>0</v>)",
        "fourth output should omit the erased max-coordinate boolean payload");
    check_contains(fourth_worksheet_xml, "rename-back-max-coordinate-scalar-erase-first",
        "fourth output should preserve the setup A1 text after scalar edge erase");
    check_contains(fourth_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "fourth output should preserve source-backed B1 after scalar edge erase");
    check_contains(fourth_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "fourth output should preserve source-backed A2 after scalar edge erase");
}

void test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_formula_erase_shrinks_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-formula-erase-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-formula-erase-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-formula-erase-second.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-max-coordinate-formula-erase-third.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientFormulaErase");
    editor.rename_sheet("TransientFormulaErase", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-max-coordinate-formula-erase-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before max-coordinate formula erase setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor formula_handle = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !formula_handle.has_pending_changes(),
        "matching reacquire before max-coordinate formula erase should keep both handles clean");
    check(sheet.name() == "Data" && formula_handle.name() == "Data",
        "saved and reacquired max-coordinate formula erase handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    formula_handle.set_cell(1048576, 16384,
        fastxlsx::CellValue::formula(R"(SUM(A1:B1)&"<edge-erase>")"));
    check(!editor.last_edit_error().has_value(),
        "max-coordinate formula erase set_cell should not create edit diagnostics");
    check(sheet.has_pending_changes() && formula_handle.has_pending_changes(),
        "max-coordinate formula erase set_cell should dirty shared handles");
    check(sheet.cell_count() == 4 && formula_handle.cell_count() == 4,
        "max-coordinate formula erase set_cell should add one sparse edge record");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = formula_handle.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Formula &&
                by_position.text_value() == R"(SUM(A1:B1)&"<edge-erase>")",
            "max-coordinate formula erase should read back through row/column overloads");
        check(by_a1.kind() == fastxlsx::CellValueKind::Formula &&
                by_a1.text_value() == R"(SUM(A1:B1)&"<edge-erase>")",
            "max-coordinate formula erase should read back through the A1 overload");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "max-coordinate formula erase sparse range should return exactly the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "max-coordinate formula erase sparse range should preserve the legal Excel boundary");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    edge_cells[0].value.text_value() == R"(SUM(A1:B1)&"<edge-erase>")",
                "max-coordinate formula erase sparse range should preserve formula text");
        }
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        formula_handle,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientFormulaErase",
        "post-recovery max-coordinate formula set before erase",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !formula_handle.has_pending_changes(),
        "second safe save_as should clean max-coordinate formula erase handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the max-coordinate formula erase handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear max-coordinate formula erase dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear max-coordinate formula erase dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear max-coordinate formula erase summaries");

    fastxlsx::WorksheetEditor after_formula_save = editor.worksheet("Data", options);
    check(!after_formula_save.has_pending_changes(),
        "matching reacquire after max-coordinate formula erase save should start clean");
    check(after_formula_save.get_cell("XFD1048576").kind() == fastxlsx::CellValueKind::Formula &&
            after_formula_save.get_cell("XFD1048576").text_value() == R"(SUM(A1:B1)&"<edge-erase>")",
        "matching reacquire after max-coordinate formula erase save should preserve the edge formula");

    after_formula_save.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "A1 erase after max-coordinate formula save should not create diagnostics");
    check(sheet.has_pending_changes() && formula_handle.has_pending_changes() &&
            after_formula_save.has_pending_changes(),
        "erasing the saved max-coordinate formula should dirty shared handles");
    check(sheet.cell_count() == 3 && formula_handle.cell_count() == 3 &&
            after_formula_save.cell_count() == 3,
        "erasing the saved max-coordinate formula should shrink the sparse record count");
    check(!after_formula_save.try_cell(1048576, 16384).has_value(),
        "erasing the saved max-coordinate formula should remove the edge record");
    check(!sheet.try_cell("XFD1048576").has_value(),
        "erasing the saved max-coordinate formula should remove the A1 edge record");
    check(threw_fastxlsx_error([&] {
        (void)formula_handle.get_cell("XFD1048576");
    }), "A1 get_cell should throw after erasing the saved max-coordinate formula");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "max-coordinate formula sparse range should be empty after erase");
    }
    check(after_formula_save.estimated_memory_usage() == sheet.estimated_memory_usage(),
        "max-coordinate formula erase should keep reacquired handle memory aligned");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        after_formula_save,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientFormulaErase",
        "post-recovery max-coordinate formula erase",
        4,
        3,
        sheet.estimated_memory_usage());

    editor.save_as(third_output);
    check(!sheet.has_pending_changes() && !formula_handle.has_pending_changes() &&
            !after_formula_save.has_pending_changes(),
        "third safe save_as should clean max-coordinate formula erase handles");
    check(editor.pending_change_count() == 5,
        "third safe save_as should count the max-coordinate formula erase handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "third safe save_as should clear max-coordinate formula erase dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "third safe save_as should clear max-coordinate formula erase dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "third safe save_as should clear max-coordinate formula erase summaries");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "source package should not contain max-coordinate formula erase mutations");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "SUM(A1:B1)",
        "source package should not contain max-coordinate formula erase text");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first max-coordinate formula erase output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientFormulaErase",
        "first max-coordinate formula erase output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-max-coordinate-formula-erase-first",
        "first output should contain the setup value before max-coordinate formula erase");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "XFD1048576",
        "first output should not contain the later max-coordinate formula erase record");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second max-coordinate formula erase output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientFormulaErase",
        "second max-coordinate formula erase output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "second max-coordinate formula erase output should refresh dimension through XFD1048576");
    check_contains(second_worksheet_xml,
        R"(<row r="1048576"><c r="XFD1048576"><f>SUM(A1:B1)&amp;"&lt;edge-erase&gt;"</f></c></row>)",
        "second output should persist escaped formula at the sparse max-coordinate row before erase");
    check_not_contains(second_worksheet_xml,
        R"(<c r="XFD1048576"><f>SUM(A1:B1)&amp;"&lt;edge-erase&gt;"</f><v>)",
        "second output should not generate a cached value for the max-coordinate formula before erase");

    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    const std::string third_workbook_xml = third_entries.at("xl/workbook.xml");
    const std::string third_worksheet_xml = third_entries.at("xl/worksheets/sheet1.xml");
    check_contains(third_workbook_xml, R"(name="Data")",
        "third max-coordinate formula erase output should keep the restored source name");
    check_not_contains(third_workbook_xml, "TransientFormulaErase",
        "third max-coordinate formula erase output should not leak the transient planned name");
    check_contains(third_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "third output should shrink dimension after erasing the max-coordinate formula");
    check_not_contains(third_worksheet_xml, "XFD1048576",
        "third output should omit the erased max-coordinate formula reference");
    check_not_contains(third_worksheet_xml, "SUM(A1:B1)",
        "third output should omit the erased max-coordinate formula payload");
    check_contains(third_worksheet_xml, "rename-back-max-coordinate-formula-erase-first",
        "third output should preserve the setup A1 text after formula edge erase");
    check_contains(third_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "third output should preserve source-backed B1 after formula edge erase");
    check_contains(third_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "third output should preserve source-backed A2 after formula edge erase");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = workbook_editor_shard_from_args(argc, argv);
        std::printf("fastxlsx.workbook_editor shard: %.*s\n",
            static_cast<int>(shard.size()), shard.data());

        if (should_run_workbook_editor_shard(shard, "public-edge")) {
            test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_erase_shrinks_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_a1_mutations();
            test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_blank_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_formula_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_scalar_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_scalar_erase_shrinks_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_max_coordinate_formula_erase_shrinks_projection();
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
