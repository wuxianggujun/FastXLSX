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

void test_public_request_full_calculation_preserves_dirty_materialized_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-full-calc-materialized-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-full-calc-materialized-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("full-calc-materialized"));
    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_change_count() == 0,
        "dirty materialized full-calc setup should not queue a Patch handoff");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "dirty materialized full-calc setup should report Data dirty");

    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "request_full_calculation with dirty materialized state should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "request_full_calculation should add one public metadata edit before materialized flush");
    check(sheet.has_pending_changes(),
        "request_full_calculation should not flush the borrowed materialized session");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "request_full_calculation should preserve dirty materialized names");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "request_full_calculation should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "request_full_calculation should preserve dirty materialized memory");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "request_full_calculation with dirty materialized state");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "save_as after request_full_calculation should clear dirty materialized state");
    check(editor.pending_change_count() == 2,
        "save_as after request_full_calculation should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after request_full_calculation should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after request_full_calculation should clear dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "save_as after request_full_calculation should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "request_full_calculation should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "request_full_calculation should not invent calcChain.xml");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "full-calc-materialized",
        "request_full_calculation save_as should still persist materialized cells");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "request_full_calculation save_as should keep the materialized dimension refresh");
}

void test_public_request_full_calculation_preserves_clean_materialized_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-full-calc-clean-materialized-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-full-calc-clean-materialized-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const fastxlsx::CellValue original_a1 = sheet.get_cell(1, 1);
    check(original_a1.kind() == fastxlsx::CellValueKind::Text &&
            original_a1.text_value() == "placeholder-a1",
        "clean materialized full-calc setup should read source data");
    check(!sheet.has_pending_changes(),
        "clean materialized full-calc setup should keep the borrowed sheet clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean materialized full-calc setup should not report dirty names");

    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "request_full_calculation with clean materialized state should clear diagnostics");
    check(editor.has_pending_changes(),
        "request_full_calculation with clean materialized state should queue workbook metadata");
    check(editor.pending_change_count() == 1,
        "request_full_calculation with clean materialized state should count one metadata edit");
    check(!sheet.has_pending_changes(),
        "request_full_calculation should not dirty the clean borrowed sheet");
    check(editor.pending_materialized_worksheet_names().empty(),
        "request_full_calculation should not report clean materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "request_full_calculation should not report clean materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "request_full_calculation should not report clean materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "request_full_calculation should not invent worksheet edit summaries");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "save_as after clean materialized full-calc request should keep the sheet clean");
    check(editor.pending_change_count() == 1,
        "save_as after clean materialized full-calc request should not add a materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after clean materialized full-calc request should keep dirty names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after clean materialized full-calc request should keep dirty cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "save_as after clean materialized full-calc request should keep dirty memory empty");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after clean materialized full-calc request should keep summaries empty");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "clean materialized full-calc save should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "clean materialized full-calc save should not invent calcChain.xml");
    check(output_entries.at("xl/worksheets/sheet1.xml") ==
            source_entries.at("xl/worksheets/sheet1.xml"),
        "clean materialized full-calc save should preserve Data worksheet bytes");
}

void test_public_request_full_calculation_preserves_saved_clean_materialized_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-full-calc-saved-clean-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-full-calc-saved-clean-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-full-calc-saved-clean-second-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("saved-clean-full-calc"));
    editor.save_as(first_output);

    check(!sheet.has_pending_changes(),
        "saved-clean full-calc setup should leave the borrowed sheet clean");
    check(editor.pending_change_count() == 1,
        "saved-clean full-calc setup should count the prior materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "saved-clean full-calc setup should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "saved-clean full-calc setup should clear dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "saved-clean full-calc setup should clear dirty materialized memory");

    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "request_full_calculation after saved-clean materialized save should clear diagnostics");
    check(editor.pending_change_count() == 2,
        "request_full_calculation after saved-clean materialized save should add one metadata edit");
    check(!sheet.has_pending_changes(),
        "request_full_calculation should not dirty a saved-clean materialized sheet");
    check(editor.pending_materialized_worksheet_names().empty(),
        "request_full_calculation after saved-clean materialized save should keep dirty names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "request_full_calculation after saved-clean materialized save should keep dirty cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "request_full_calculation after saved-clean materialized save should keep dirty memory empty");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes(),
        "second save_as after saved-clean full-calc request should keep the sheet clean");
    check(editor.pending_change_count() == 2,
        "second save_as after saved-clean full-calc request should not add a new handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second save_as after saved-clean full-calc request should keep dirty names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "second save_as after saved-clean full-calc request should keep dirty cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second save_as after saved-clean full-calc request should keep dirty memory empty");

    const auto output_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "saved-clean full-calc save should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "saved-clean full-calc save should not invent calcChain.xml");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "saved-clean-full-calc",
        "saved-clean full-calc save should preserve the prior materialized projection");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "saved-clean full-calc save should not reload the old source cell");
}

void test_public_request_full_calculation_allows_later_materialized_edit()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-full-calc-before-materialize-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-full-calc-before-materialize-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "full-calc-before-materialize setup should clear diagnostics");
    check(editor.has_pending_changes(),
        "full-calc-before-materialize setup should queue workbook metadata");
    check(editor.pending_change_count() == 1,
        "full-calc-before-materialize setup should count one metadata edit");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc-before-materialize setup should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc-before-materialize setup should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc-before-materialize setup should not expose dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc-before-materialize setup should not invent worksheet summaries");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check(!sheet.has_pending_changes(),
        "worksheet() after request_full_calculation should materialize Data cleanly");
    check(editor.pending_change_count() == 1,
        "clean materialization after request_full_calculation should keep the metadata edit count");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean materialization after request_full_calculation should keep dirty names empty");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("full-calc-before-materialize"));
    check(sheet.has_pending_changes(),
        "later materialized edit after request_full_calculation should dirty the sheet");
    check(editor.pending_change_count() == 1,
        "later materialized edit should not flush before save_as");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "later materialized edit should expose Data dirty");
    check(editor.pending_materialized_cell_count() == sheet.cell_count(),
        "later materialized edit should expose its sparse cell count");
    check(editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "later materialized edit should expose its sparse memory estimate");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "save_as after full-calc-before-materialize edit should clear dirty state");
    check(editor.pending_change_count() == 2,
        "save_as after full-calc-before-materialize edit should count metadata plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after full-calc-before-materialize edit should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after full-calc-before-materialize edit should clear dirty cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "save_as after full-calc-before-materialize edit should clear dirty memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc-before-materialize save should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc-before-materialize save should not invent calcChain.xml");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "full-calc-before-materialize",
        "full-calc-before-materialize save should persist the later materialized edit");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "full-calc-before-materialize save should not preserve the old source cell");
}

void test_public_request_full_calculation_preserves_later_clean_materialization()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-full-calc-before-clean-materialize-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-full-calc-before-clean-materialize-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.request_full_calculation();
    check(editor.pending_change_count() == 1,
        "full-calc-before-clean-materialize setup should count one metadata edit");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc-before-clean-materialize setup should not expose dirty materialized names");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const fastxlsx::CellValue original_a1 = sheet.get_cell(1, 1);
    check(original_a1.kind() == fastxlsx::CellValueKind::Text &&
            original_a1.text_value() == "placeholder-a1",
        "worksheet() after request_full_calculation should read source cells");
    check(!sheet.has_pending_changes(),
        "worksheet() after request_full_calculation should keep clean materialized state");
    check(editor.pending_change_count() == 1,
        "clean materialization after request_full_calculation should not add a handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean materialization after request_full_calculation should keep dirty names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "clean materialization after request_full_calculation should keep dirty cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "clean materialization after request_full_calculation should keep dirty memory empty");
    check(editor.pending_worksheet_edits().empty(),
        "clean materialization after request_full_calculation should not invent summaries");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "save_as after full-calc-before-clean-materialize should keep the sheet clean");
    check(editor.pending_change_count() == 1,
        "save_as after full-calc-before-clean-materialize should not add a materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after full-calc-before-clean-materialize should keep dirty names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after full-calc-before-clean-materialize should keep dirty cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "save_as after full-calc-before-clean-materialize should keep dirty memory empty");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after full-calc-before-clean-materialize should keep summaries empty");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc-before-clean-materialize save should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc-before-clean-materialize save should not invent calcChain.xml");
    check(output_entries.at("xl/worksheets/sheet1.xml") ==
            source_entries.at("xl/worksheets/sheet1.xml"),
        "full-calc-before-clean-materialize save should preserve source worksheet bytes");
}

void test_public_request_full_calculation_preserves_later_replacement_guard()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-full-calc-before-replacement-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-full-calc-before-replacement-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "full-calc-before-replacement setup should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "full-calc-before-replacement setup should count one metadata edit");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("full-calc-before-replacement")}});
    const std::size_t replacement_memory =
        editor.estimated_pending_replacement_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "replacement after full-calc request should keep diagnostics clear");
    check(editor.pending_change_count() == 2,
        "replacement after full-calc request should count metadata plus replacement");
    check(editor.pending_replacement_cell_count() == 1,
        "replacement after full-calc request should expose replacement cell diagnostics");
    check(editor.pending_replacement_worksheet_names() == std::vector<std::string>{"Data"},
        "replacement after full-calc request should expose Data replacement diagnostics");
    check(replacement_memory > 0,
        "replacement after full-calc request should expose replacement memory diagnostics");

    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("Data");
    }), "try_worksheet should reject a full-calc queued replacement");
    check(!editor.last_edit_error().has_value(),
        "try_worksheet full-calc replacement guard should not update last_edit_error");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data");
    }), "worksheet should reject a full-calc queued replacement");
    check(!editor.last_edit_error().has_value(),
        "worksheet full-calc replacement guard should not update last_edit_error");
    check(editor.pending_change_count() == 2,
        "full-calc replacement guard should preserve queued public edit count");
    check(editor.pending_replacement_cell_count() == 1,
        "full-calc replacement guard should preserve replacement cell diagnostics");
    check(editor.pending_replacement_worksheet_names() == std::vector<std::string>{"Data"},
        "full-calc replacement guard should preserve replacement worksheet names");
    check(editor.estimated_pending_replacement_memory_usage() == replacement_memory,
        "full-calc replacement guard should preserve replacement memory diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc replacement guard should not create materialized diagnostics");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc queued replacement save should persist workbook calc metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc queued replacement save should not invent calcChain.xml");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "full-calc-before-replacement",
        "full-calc queued replacement save should persist the replacement payload");
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
        const std::size_t replacement_memory =
            editor.estimated_pending_replacement_memory_usage();
        check(editor.pending_change_count() == 1,
            "queued sheetData replacement should count one public edit before materialization guards");
        check(editor.pending_replacement_cell_count() == 1,
            "queued sheetData replacement should expose replacement cell diagnostics");
        check(editor.pending_replacement_worksheet_names()
                == std::vector<std::string>{"Data"},
            "queued sheetData replacement should expose Data replacement diagnostics");
        check(replacement_memory > 0,
            "queued sheetData replacement should expose replacement memory diagnostics");
        check(threw_fastxlsx_error([&] {
            (void)editor.try_worksheet("Data");
        }), "try_worksheet should reject a worksheet with queued sheetData replacement");
        check(!editor.last_edit_error().has_value(),
            "try_worksheet replacement-mix rejection should not update last_edit_error");
        check(threw_fastxlsx_error([&] {
            (void)editor.worksheet("Data");
        }), "worksheet should reject a worksheet with queued sheetData replacement");
        check(!editor.last_edit_error().has_value(),
            "worksheet replacement-mix rejection should not update last_edit_error");
        check(editor.pending_change_count() == 1,
            "try_worksheet replacement-mix rejection should preserve queued edits");
        check(editor.pending_replacement_cell_count() == 1,
            "replacement-mix rejection should preserve replacement cell diagnostics");
        check(editor.pending_replacement_worksheet_names()
                == std::vector<std::string>{"Data"},
            "replacement-mix rejection should preserve replacement worksheet names");
        check(editor.estimated_pending_replacement_memory_usage() == replacement_memory,
            "replacement-mix rejection should preserve replacement memory diagnostics");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "replacement-mix rejection should not create materialized diagnostics");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-try-worksheet-replacement-mix-output.xlsx");
        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "queued",
            "replacement-mix rejection should still allow the queued replacement to save");
    }
}

void test_public_worksheet_editor_rejects_catalog_edits_after_materialization()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-rename-guard-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-rename-guard-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("materialized-survives-guard"));

    const WorkbookEditorPublicCatalogSnapshot catalog_before =
        workbook_editor_public_catalog_snapshot(editor);
    const std::size_t cell_count_before = sheet.cell_count();
    const std::size_t memory_before = sheet.estimated_memory_usage();

    const std::optional<std::string> guard_error =
        check_public_same_sheet_rename_then_replacement_guard_sequence(
            editor,
            "Data",
            "BlockedRename",
            "blocked-replacement",
            "dirty materialized catalog guard");

    check(guard_error.has_value(),
        "dirty materialized catalog guard should expose the final replacement guard");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before, "dirty materialized catalog guard");
    check(editor.has_worksheet("Data"),
        "dirty materialized catalog guard should keep the original planned name");
    check(!editor.has_worksheet("BlockedRename"),
        "dirty materialized catalog guard should not expose the rejected rename");
    check(!editor.has_pending_replacement("Data") &&
            !editor.has_pending_replacement("BlockedRename"),
        "dirty materialized catalog guard should not queue replacement diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "dirty materialized catalog guard");
    check(sheet.has_pending_changes(),
        "dirty materialized catalog guard should keep the borrowed handle dirty");
    check(sheet.cell_count() == cell_count_before,
        "dirty materialized catalog guard should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == memory_before,
        "dirty materialized catalog guard should preserve sparse memory estimate");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"Data"},
        "dirty materialized catalog guard should keep Data dirty");
    check(editor.pending_materialized_cell_count() == cell_count_before,
        "dirty materialized catalog guard should keep materialized cell diagnostics");
    check(editor.estimated_pending_materialized_memory_usage() == memory_before,
        "dirty materialized catalog guard should keep materialized memory diagnostics");
    check(editor.pending_change_count() == 0,
        "dirty materialized catalog guard should not queue public edits before save");

    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "dirty materialized catalog guard should keep one worksheet summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "dirty materialized catalog guard should keep Data summary names");
            check(!summary.renamed && !summary.sheet_data_replaced,
                "dirty materialized catalog guard should not mark rejected Patch edits");
            check(summary.materialized_dirty &&
                    summary.materialized_cell_count == cell_count_before &&
                    summary.estimated_materialized_memory_usage == memory_before,
                "dirty materialized catalog guard should keep materialized summary fields");
        }
    }

    check_public_inspection_preserves_last_edit_error(editor, guard_error);

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "safe save after dirty materialized catalog guard should clean the handle");
    check(editor.pending_change_count() == 1,
        "safe save after dirty materialized catalog guard should record only the materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "safe save after dirty materialized catalog guard should clear materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "safe save after dirty materialized catalog guard");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "safe save after dirty materialized catalog guard should preserve Data catalog name");
    check_not_contains(output_entries.at("xl/workbook.xml"), "BlockedRename",
        "safe save after dirty materialized catalog guard should not leak rejected rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "materialized-survives-guard",
        "safe save after dirty materialized catalog guard should persist materialized cells");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "blocked-replacement",
        "safe save after dirty materialized catalog guard should not leak rejected replacement");
}

void test_public_worksheet_editor_rejects_catalog_edits_after_clean_materialization()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-clean-rename-guard-source.xlsx");
    const std::filesystem::path readonly_output =
        artifact("fastxlsx-workbook-editor-public-clean-rename-guard-readonly-output.xlsx");
    const std::filesystem::path first_saved_output =
        artifact("fastxlsx-workbook-editor-public-clean-rename-guard-first-output.xlsx");
    const std::filesystem::path second_saved_output =
        artifact("fastxlsx-workbook-editor-public-clean-rename-guard-second-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const WorkbookEditorPublicCatalogSnapshot catalog_before =
            workbook_editor_public_catalog_snapshot(editor);

        const std::optional<std::string> guard_error =
            check_public_same_sheet_rename_then_replacement_guard_sequence(
                editor,
                "Data",
                "ReadonlyBlockedRename",
                "readonly-blocked-replacement",
                "read-only materialized catalog guard");

        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before, "read-only materialized catalog guard");
        check(!editor.has_worksheet("ReadonlyBlockedRename"),
            "read-only materialized catalog guard should not expose rejected rename");
        check(!editor.has_pending_replacement("Data") &&
                !editor.has_pending_replacement("ReadonlyBlockedRename"),
            "read-only materialized catalog guard should not queue replacements");
        check_public_failed_same_sheet_patch_readonly_clean_state(
            editor, sheet, guard_error, "Data", "read-only materialized catalog guard");

        editor.save_as(readonly_output);
        const auto output_entries = fastxlsx::test::read_zip_entries(readonly_output);
        check(output_entries == source_entries,
            "read-only materialized catalog guard save_as should copy source entries");
        check_not_contains(output_entries.at("xl/workbook.xml"), "ReadonlyBlockedRename",
            "read-only materialized catalog guard output should not leak rejected rename");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "readonly-blocked-replacement",
            "read-only materialized catalog guard output should not leak replacement");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        sheet.set_cell(1, 1, fastxlsx::CellValue::text("saved-clean-survives-guard"));
        editor.save_as(first_saved_output);
        check(!sheet.has_pending_changes(),
            "saved-clean catalog guard precondition should start from a clean handle");

        const std::size_t saved_pending_count = editor.pending_change_count();
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> saved_summaries =
            editor.pending_worksheet_edits();
        const WorkbookEditorPublicCatalogSnapshot catalog_before =
            workbook_editor_public_catalog_snapshot(editor);

        const std::optional<std::string> guard_error =
            check_public_same_sheet_rename_then_replacement_guard_sequence(
                editor,
                "Data",
                "SavedBlockedRename",
                "saved-blocked-replacement",
                "saved-clean materialized catalog guard");

        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before, "saved-clean materialized catalog guard");
        check(!editor.has_worksheet("SavedBlockedRename"),
            "saved-clean materialized catalog guard should not expose rejected rename");
        check(!editor.has_pending_replacement("Data") &&
                !editor.has_pending_replacement("SavedBlockedRename"),
            "saved-clean materialized catalog guard should not queue replacements");
        check_public_failed_same_sheet_patch_saved_clean_state(
            editor,
            sheet,
            saved_pending_count,
            saved_summaries,
            guard_error,
            "Data",
            "saved-clean materialized catalog guard");

        editor.save_as(second_saved_output);
        check(editor.pending_change_count() == saved_pending_count,
            "saved-clean materialized catalog guard save_as should not add handoffs");
        const auto first_entries = fastxlsx::test::read_zip_entries(first_saved_output);
        const auto second_entries = fastxlsx::test::read_zip_entries(second_saved_output);
        check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
            "saved-clean-survives-guard",
            "saved-clean materialized catalog guard first output should keep saved value");
        check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            "saved-clean-survives-guard",
            "saved-clean materialized catalog guard second output should keep saved value");
        check_not_contains(second_entries.at("xl/workbook.xml"), "SavedBlockedRename",
            "saved-clean materialized catalog guard output should not leak rejected rename");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            "saved-blocked-replacement",
            "saved-clean materialized catalog guard output should not leak replacement");
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
            test_public_request_full_calculation_preserves_dirty_materialized_session();
            test_public_request_full_calculation_preserves_clean_materialized_session();
            test_public_request_full_calculation_preserves_saved_clean_materialized_session();
            test_public_request_full_calculation_allows_later_materialized_edit();
            test_public_request_full_calculation_preserves_later_clean_materialization();
            test_public_request_full_calculation_preserves_later_replacement_guard();
            test_public_worksheet_editor_rejects_catalog_edits_after_materialization();
            test_public_worksheet_editor_rejects_catalog_edits_after_clean_materialization();
            test_public_worksheet_editor_reacquire_reuses_dirty_session();
            test_public_worksheet_editor_reacquire_after_save_reuses_session();
            test_public_worksheet_editor_post_save_reacquire_preserves_clean_diagnostics();
            test_public_worksheet_editor_post_save_option_mismatch_preserves_session();
            test_public_worksheet_editor_post_save_summary_tracks_reacquire_dirty_state();
            test_public_worksheet_editor_post_save_summary_preserves_rename_context();
            test_public_worksheet_editor_failed_save_as_preserves_renamed_summary_dirty_state();
            test_public_worksheet_editor_renamed_materialized_diagnostics_follow_planned_name();
            test_public_worksheet_editor_rename_back_materialized_diagnostics_use_source_name();
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
