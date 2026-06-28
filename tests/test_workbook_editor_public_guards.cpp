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
    return shard == "all" || shard == "public-guards";
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

void check_workbook_editor_no_targeted_cell_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(editor.pending_targeted_cell_replacement_count() == 0,
        prefix + " should not expose targeted cell patches");
    check(editor.pending_targeted_cell_replacement_worksheet_names().empty(),
        prefix + " should not expose targeted cell worksheet names");
    check(editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0,
        prefix + " should not expose targeted cell XML bytes");
    check(!editor.has_pending_targeted_cell_replacement("Data"),
        prefix + " should not report Data targeted cell patches");
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

    (void)editor.has_pending_targeted_cell_replacement("Data");
    check_inspection_state("has_pending_targeted_cell_replacement");

    (void)editor.estimated_pending_targeted_cell_replacement_xml_bytes();
    check_inspection_state("estimated_pending_targeted_cell_replacement_xml_bytes");

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
    check(threw_fastxlsx_error([&] {
        editor.replace_cells("Data",
            {{{1, 1}, fastxlsx::CellValue::text("blocked-public-targeted")}});
    }), "public replace_cells should reject a materialized same sheet");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedPublicRename"); }),
        "public rename_sheet should reject a materialized same sheet");
    check(editor.last_edit_error().has_value(),
        "blocked same-sheet operations should record last_edit_error");
    check_workbook_editor_no_targeted_cell_diagnostics(
        editor, "blocked public same-sheet targeted cells");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "cannot rename sheet after materializing planned worksheet session",
            "blocked same-sheet rename should replace targeted-cell diagnostic");
    }

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
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "blocked-public-targeted",
        "blocked same-sheet targeted cells should not leak into output");
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
    check_workbook_editor_no_targeted_cell_diagnostics(
        editor, "read-only same-sheet replacement");
    check_public_inspection_preserves_last_edit_error(editor, replacement_error);

    bool targeted_failed = false;
    try {
        editor.replace_cells("Data",
            {{{1, 1}, fastxlsx::CellValue::text("blocked-readonly-targeted")}});
    } catch (const fastxlsx::FastXlsxError& error) {
        targeted_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "read-only same-sheet targeted cells should replace last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "read-only same-sheet targeted diagnostic should match thrown error");
            check_contains(*last_error, "WorkbookEditor::replace_cells() failed",
                "read-only same-sheet targeted cells should report public wrapper");
            check_contains(*last_error,
                "cannot replace cells after materializing planned worksheet session",
                "read-only same-sheet targeted cells should report materialized-session guard");
            check_not_contains(*last_error, "replace sheet data",
                "read-only same-sheet targeted cells should replace the replacement diagnostic");
        }
    }
    check(targeted_failed,
        "read-only materialized session should block same-sheet targeted cells");
    const std::optional<std::string> targeted_error = editor.last_edit_error();
    check_readonly_state("after rejected targeted cells", targeted_error);
    check_workbook_editor_no_targeted_cell_diagnostics(
        editor, "read-only same-sheet targeted rejection");
    check_public_inspection_preserves_last_edit_error(editor, targeted_error);

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
            check_not_contains(*last_error, "replace cells",
                "read-only same-sheet rename should replace the targeted-cell diagnostic");
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
    check_workbook_editor_no_targeted_cell_diagnostics(
        editor, "saved-clean same-sheet replacement");
    check_public_inspection_preserves_last_edit_error(editor, replacement_error);

    bool targeted_failed = false;
    try {
        editor.replace_cells("Data",
            {{{1, 1}, fastxlsx::CellValue::text("blocked-saved-clean-targeted")}});
    } catch (const fastxlsx::FastXlsxError& error) {
        targeted_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "saved-clean same-sheet targeted cells should replace last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "saved-clean same-sheet targeted diagnostic should match thrown error");
            check_contains(*last_error, "WorkbookEditor::replace_cells() failed",
                "saved-clean same-sheet targeted cells should report public wrapper");
            check_contains(*last_error,
                "cannot replace cells after materializing planned worksheet session",
                "saved-clean same-sheet targeted cells should report materialized-session guard");
            check_not_contains(*last_error, "replace sheet data",
                "saved-clean same-sheet targeted cells should replace the replacement diagnostic");
        }
    }
    check(targeted_failed,
        "saved-clean materialized session should block same-sheet targeted cells");
    const std::optional<std::string> targeted_error = editor.last_edit_error();
    check_saved_clean_state("after rejected saved-clean targeted cells", targeted_error);
    check_workbook_editor_no_targeted_cell_diagnostics(
        editor, "saved-clean same-sheet targeted rejection");
    check_public_inspection_preserves_last_edit_error(editor, targeted_error);

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
            check_not_contains(*last_error, "replace cells",
                "saved-clean same-sheet rename should replace the targeted-cell diagnostic");
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
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "blocked-saved-clean-targeted",
        "rejected saved-clean targeted cells should not leak into output");
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

void test_public_worksheet_editor_same_sheet_guard_snapshot_reads_preserve_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-guard-snapshot-reads-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-guard-snapshot-reads-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = data.get_cell(1, 1);
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "guard snapshot-read setup should materialize Data from source");
    check(!data.has_pending_changes(),
        "guard snapshot-read setup should keep Data clean");

    const std::size_t data_cell_count = data.cell_count();
    const std::size_t data_memory = data.estimated_memory_usage();
    const std::vector<fastxlsx::WorksheetCellSnapshot> full_snapshot =
        data.sparse_cells();
    const std::vector<fastxlsx::WorksheetCellSnapshot> range_snapshot =
        data.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    const std::vector<fastxlsx::WorksheetCellSnapshot> a1_snapshot =
        data.sparse_cells("A1:B2");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        data.column_cells(1);

    const std::optional<std::string> guard_error =
        check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("guard-snapshot-read-blocked")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "guard snapshot-read same-sheet replacement");

    const std::vector<fastxlsx::WorksheetCellReference> batch {
        {1, 1},
        {2, 1},
        {5, 5},
        {1, 1},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> batch_snapshot =
        data.sparse_cells(batch);

    check(data.sparse_cells().size() == full_snapshot.size(),
        "guard snapshot reads should preserve full sparse snapshot size");
    check(data.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2}).size()
            == range_snapshot.size(),
        "guard snapshot reads should preserve range sparse snapshot size");
    check(data.sparse_cells("A1:B2").size() == a1_snapshot.size(),
        "guard snapshot reads should preserve A1 range snapshot size");
    check(data.row_cells(1).size() == row_one.size(),
        "guard snapshot reads should preserve row snapshot size");
    check(data.column_cells(1).size() == column_one.size(),
        "guard snapshot reads should preserve column snapshot size");
    check(batch_snapshot.size() == 3,
        "guard snapshot reads should return requested existing cells and duplicate references");
    if (batch_snapshot.size() == 3) {
        check(batch_snapshot[0].reference.row == 1 && batch_snapshot[0].reference.column == 1 &&
                batch_snapshot[1].reference.row == 2 && batch_snapshot[1].reference.column == 1 &&
                batch_snapshot[2].reference.row == 1 && batch_snapshot[2].reference.column == 1,
            "guard snapshot reads should keep batch source order and duplicates");
    }
    check(editor.last_edit_error() == guard_error,
        "guard snapshot reads should preserve the same-sheet diagnostic");
    check(!data.has_pending_changes(),
        "guard snapshot reads should keep Data clean");
    check(!editor.has_pending_changes(),
        "guard snapshot reads should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "guard snapshot reads should not queue public edits");
    check(editor.pending_worksheet_edits().empty(),
        "guard snapshot reads should keep pending summaries empty");
    check(editor.pending_materialized_worksheet_names().empty(),
        "guard snapshot reads should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "guard snapshot reads should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "guard snapshot reads should keep dirty materialized memory empty");
    check_public_preserved_sheet_diagnostics(
        data, data_cell_count, data_memory, "Data",
        "guard snapshot reads");
    check_public_inspection_preserves_last_edit_error(editor, guard_error);

    editor.save_as(output);
    check(editor.last_edit_error() == guard_error,
        "guard snapshot-read no-op save_as should preserve the diagnostic");
    check(!data.has_pending_changes(),
        "guard snapshot-read no-op save_as should keep Data clean");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "guard snapshot-read no-op output should remain copy-original");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-snapshot-read-blocked",
        "guard snapshot-read rejected replacement should not leak into output");
}

void test_public_worksheet_editor_same_sheet_guard_scalar_reads_preserve_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-guard-scalar-reads-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-guard-scalar-reads-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = data.get_cell("A1");
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "guard scalar-read setup should materialize Data from source");
    check(!data.has_pending_changes(),
        "guard scalar-read setup should keep Data clean");

    const std::size_t data_cell_count = data.cell_count();
    const std::size_t data_memory = data.estimated_memory_usage();
    check(!data.try_cell(5, 5).has_value(),
        "guard scalar-read setup should leave missing cells absent");

    const std::optional<std::string> guard_error =
        check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("guard-scalar-read-blocked")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "guard scalar-read same-sheet replacement");

    const std::optional<fastxlsx::CellValue> existing_cell = data.try_cell(1, 1);
    check(existing_cell.has_value() &&
            existing_cell->kind() == fastxlsx::CellValueKind::Text &&
            existing_cell->text_value() == "placeholder-a1",
        "guard scalar reads should keep existing try_cell value readable");
    const fastxlsx::CellValue a1_cell = data.get_cell("A1");
    check(a1_cell.kind() == fastxlsx::CellValueKind::Text &&
            a1_cell.text_value() == "placeholder-a1",
        "guard scalar reads should keep existing A1 get_cell value readable");
    check(!data.try_cell("E5").has_value(),
        "guard scalar reads should keep missing A1 try_cell absent");
    check(threw_fastxlsx_error([&] { (void)data.get_cell(5, 5); }),
        "guard scalar reads should keep missing get_cell failure behavior");
    check(data.cell_count() == data_cell_count,
        "guard scalar reads should preserve sparse count");
    check(data.estimated_memory_usage() == data_memory,
        "guard scalar reads should preserve sparse memory");
    check(editor.last_edit_error() == guard_error,
        "guard scalar reads should preserve the same-sheet diagnostic");
    check(!data.has_pending_changes(),
        "guard scalar reads should keep Data clean");
    check(!editor.has_pending_changes(),
        "guard scalar reads should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "guard scalar reads should not queue public edits");
    check(editor.pending_worksheet_edits().empty(),
        "guard scalar reads should keep pending summaries empty");
    check(editor.pending_materialized_worksheet_names().empty(),
        "guard scalar reads should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "guard scalar reads should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "guard scalar reads should keep dirty materialized memory empty");
    check_public_preserved_sheet_diagnostics(
        data, data_cell_count, data_memory, "Data",
        "guard scalar reads");
    check_public_inspection_preserves_last_edit_error(editor, guard_error);

    editor.save_as(output);
    check(editor.last_edit_error() == guard_error,
        "guard scalar-read no-op save_as should preserve the diagnostic");
    check(!data.has_pending_changes(),
        "guard scalar-read no-op save_as should keep Data clean");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "guard scalar-read no-op output should remain copy-original");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-scalar-read-blocked",
        "guard scalar-read rejected replacement should not leak into output");
}

void test_public_worksheet_editor_same_sheet_guard_invalid_reads_preserve_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-guard-invalid-reads-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-guard-invalid-reads-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = data.get_cell("A1");
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "guard invalid-read setup should materialize Data from source");
    check(!data.has_pending_changes(),
        "guard invalid-read setup should keep Data clean");

    const std::size_t data_cell_count = data.cell_count();
    const std::size_t data_memory = data.estimated_memory_usage();

    const std::optional<std::string> guard_error =
        check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("guard-invalid-read-blocked")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "guard invalid-read same-sheet replacement");

    check(threw_fastxlsx_error([&] { (void)data.try_cell(0, 1); }),
        "guard invalid reads should reject row zero");
    check(threw_fastxlsx_error([&] { (void)data.get_cell(1, 0); }),
        "guard invalid reads should reject column zero");
    check(threw_fastxlsx_error([&] { (void)data.try_cell("a1"); }),
        "guard invalid reads should reject lowercase A1 references");
    check(threw_fastxlsx_error([&] { (void)data.get_cell("XFE1"); }),
        "guard invalid reads should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)data.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "guard invalid reads should reject invalid range coordinates");
    check(threw_fastxlsx_error([&] {
        (void)data.sparse_cells(fastxlsx::CellRange {2, 1, 1, 1});
    }), "guard invalid reads should reject reversed ranges");
    check(threw_fastxlsx_error([&] { (void)data.row_cells(0); }),
        "guard invalid reads should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)data.column_cells(16385); }),
        "guard invalid reads should reject column_cells column overflow");

    check(editor.last_edit_error() == guard_error,
        "guard invalid reads should preserve the same-sheet diagnostic");
    check(!data.has_pending_changes(),
        "guard invalid reads should keep Data clean");
    check(!editor.has_pending_changes(),
        "guard invalid reads should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "guard invalid reads should not queue public edits");
    check(editor.pending_worksheet_edits().empty(),
        "guard invalid reads should keep pending summaries empty");
    check(editor.pending_materialized_worksheet_names().empty(),
        "guard invalid reads should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "guard invalid reads should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "guard invalid reads should keep dirty materialized memory empty");
    check_public_preserved_sheet_diagnostics(
        data, data_cell_count, data_memory, "Data",
        "guard invalid reads");
    check_public_inspection_preserves_last_edit_error(editor, guard_error);

    editor.save_as(output);
    check(editor.last_edit_error() == guard_error,
        "guard invalid-read no-op save_as should preserve the diagnostic");
    check(!data.has_pending_changes(),
        "guard invalid-read no-op save_as should keep Data clean");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "guard invalid-read no-op output should remain copy-original");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-invalid-read-blocked",
        "guard invalid-read rejected replacement should not leak into output");
}

void test_public_worksheet_editor_same_sheet_guard_invalid_mutations_replace_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-guard-invalid-mutations-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-guard-invalid-mutations-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = data.get_cell("A1");
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "guard invalid-mutation setup should materialize Data from source");
    check(!data.has_pending_changes(),
        "guard invalid-mutation setup should keep Data clean");

    const std::size_t data_cell_count = data.cell_count();
    const std::size_t data_memory = data.estimated_memory_usage();

    const std::optional<std::string> guard_error =
        check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("guard-invalid-mutation-blocked")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "guard invalid-mutation same-sheet replacement");

    check(threw_fastxlsx_error([&] {
        data.set_cell(0, 1,
            fastxlsx::CellValue::text("guard-rejected-invalid-mutation-row-zero"));
    }), "guard invalid mutations should reject row-zero set_cell");
    const std::optional<std::string> invalid_set_error = editor.last_edit_error();
    check(invalid_set_error.has_value(),
        "guard invalid set_cell should replace the same-sheet diagnostic");
    if (invalid_set_error.has_value()) {
        check_contains(*invalid_set_error,
            "WorksheetEditor cell coordinate is invalid",
            "guard invalid set_cell should report the coordinate diagnostic");
        check_not_contains(*invalid_set_error,
            public_materialized_guard_action_fragment(
                PublicMaterializedGuardDiagnostic::ReplaceSheetData),
            "guard invalid set_cell should replace the same-sheet guard diagnostic");
    }
    check(invalid_set_error != guard_error,
        "guard invalid set_cell should not preserve the stale guard diagnostic");

    check(threw_fastxlsx_error([&] { data.erase_cell(1, 16385); }),
        "guard invalid mutations should reject column-overflow erase_cell");
    const std::optional<std::string> invalid_erase_error = editor.last_edit_error();
    check(invalid_erase_error.has_value(),
        "guard invalid erase_cell should keep invalid mutation diagnostics populated");
    if (invalid_erase_error.has_value()) {
        check_contains(*invalid_erase_error,
            "WorksheetEditor cell coordinate is invalid",
            "guard invalid erase_cell should report the coordinate diagnostic");
        check_not_contains(*invalid_erase_error,
            public_materialized_guard_action_fragment(
                PublicMaterializedGuardDiagnostic::ReplaceSheetData),
            "guard invalid erase_cell should not restore the same-sheet guard diagnostic");
    }

    check(!data.has_pending_changes(),
        "guard invalid mutations should keep Data clean");
    check(!editor.has_pending_changes(),
        "guard invalid mutations should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "guard invalid mutations should not queue public edits");
    check(editor.pending_worksheet_edits().empty(),
        "guard invalid mutations should keep pending summaries empty");
    check(editor.pending_materialized_worksheet_names().empty(),
        "guard invalid mutations should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "guard invalid mutations should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "guard invalid mutations should keep dirty materialized memory empty");
    check_public_preserved_sheet_diagnostics(
        data, data_cell_count, data_memory, "Data",
        "guard invalid mutations");
    check_public_inspection_preserves_last_edit_error(editor, invalid_erase_error);

    editor.save_as(output);
    check(editor.last_edit_error() == invalid_erase_error,
        "guard invalid-mutation no-op save_as should preserve invalid mutation diagnostics");
    check(!data.has_pending_changes(),
        "guard invalid-mutation no-op save_as should keep Data clean");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "guard invalid-mutation no-op output should remain copy-original");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-invalid-mutation-blocked",
        "guard invalid-mutation rejected replacement should not leak into output");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-rejected-invalid-mutation-row-zero",
        "guard invalid-mutation rejected set_cell payload should not leak into output");
}

void test_public_worksheet_editor_same_sheet_guard_invalid_reads_preserve_invalid_mutation_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-guard-invalid-read-after-mutation-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-guard-invalid-read-after-mutation-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = data.get_cell("A1");
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "guard invalid-read-after-mutation setup should materialize Data from source");
    check(!data.has_pending_changes(),
        "guard invalid-read-after-mutation setup should keep Data clean");

    const std::size_t data_cell_count = data.cell_count();
    const std::size_t data_memory = data.estimated_memory_usage();

    (void)check_public_same_sheet_guard_failure(
        editor,
        [&] {
            editor.replace_sheet_data("Data",
                {{fastxlsx::CellValue::text("guard-invalid-read-after-mutation-blocked")}});
        },
        PublicMaterializedGuardDiagnostic::ReplaceSheetData,
        "guard invalid-read-after-mutation same-sheet replacement");

    check(threw_fastxlsx_error([&] {
        data.set_cell(0, 1,
            fastxlsx::CellValue::text("guard-invalid-read-after-mutation-rejected"));
    }), "guard invalid-read-after-mutation should reject row-zero set_cell");
    const std::optional<std::string> invalid_mutation_error =
        editor.last_edit_error();
    check(invalid_mutation_error.has_value(),
        "guard invalid-read-after-mutation should record invalid mutation diagnostics");
    if (invalid_mutation_error.has_value()) {
        check_contains(*invalid_mutation_error,
            "WorksheetEditor cell coordinate is invalid",
            "guard invalid-read-after-mutation should report invalid mutation coordinates");
        check_not_contains(*invalid_mutation_error,
            public_materialized_guard_action_fragment(
                PublicMaterializedGuardDiagnostic::ReplaceSheetData),
            "guard invalid-read-after-mutation should replace the same-sheet guard diagnostic");
    }

    check(threw_fastxlsx_error([&] { (void)data.get_cell(1, 0); }),
        "guard invalid reads after invalid mutation should reject column zero");
    check(threw_fastxlsx_error([&] { (void)data.get_cell("a1"); }),
        "guard invalid reads after invalid mutation should reject lowercase A1 references");
    check(threw_fastxlsx_error([&] {
        (void)data.sparse_cells(fastxlsx::CellRange {2, 1, 1, 1});
    }), "guard invalid reads after invalid mutation should reject reversed ranges");
    check(threw_fastxlsx_error([&] { (void)data.row_cells(0); }),
        "guard invalid reads after invalid mutation should reject row zero snapshots");
    check(threw_fastxlsx_error([&] { (void)data.column_cells(16385); }),
        "guard invalid reads after invalid mutation should reject column overflow snapshots");

    check(editor.last_edit_error() == invalid_mutation_error,
        "guard invalid reads after invalid mutation should preserve the invalid mutation diagnostic");
    check(!data.has_pending_changes(),
        "guard invalid reads after invalid mutation should keep Data clean");
    check(!editor.has_pending_changes(),
        "guard invalid reads after invalid mutation should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "guard invalid reads after invalid mutation should not queue public edits");
    check(editor.pending_worksheet_edits().empty(),
        "guard invalid reads after invalid mutation should keep pending summaries empty");
    check(editor.pending_materialized_worksheet_names().empty(),
        "guard invalid reads after invalid mutation should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "guard invalid reads after invalid mutation should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "guard invalid reads after invalid mutation should keep dirty materialized memory empty");
    check_public_preserved_sheet_diagnostics(
        data, data_cell_count, data_memory, "Data",
        "guard invalid reads after invalid mutation");
    check_public_inspection_preserves_last_edit_error(editor, invalid_mutation_error);

    editor.save_as(output);
    check(editor.last_edit_error() == invalid_mutation_error,
        "guard invalid-read-after-mutation no-op save_as should preserve invalid mutation diagnostics");
    check(!data.has_pending_changes(),
        "guard invalid-read-after-mutation no-op save_as should keep Data clean");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "guard invalid-read-after-mutation no-op output should remain copy-original");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-invalid-read-after-mutation-blocked",
        "guard invalid-read-after-mutation rejected replacement should not leak into output");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-invalid-read-after-mutation-rejected",
        "guard invalid-read-after-mutation rejected set_cell payload should not leak into output");
}

void test_public_worksheet_editor_same_sheet_guard_empty_batch_noops_clear_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-guard-empty-batch-noops-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-guard-empty-batch-noops-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = data.get_cell("A1");
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "guard empty-batch no-op setup should materialize Data from source");
    check(!data.has_pending_changes(),
        "guard empty-batch no-op setup should keep Data clean");

    const std::size_t data_cell_count = data.cell_count();
    const std::size_t data_memory = data.estimated_memory_usage();

    (void)check_public_same_sheet_guard_failure(
        editor,
        [&] {
            editor.replace_sheet_data("Data",
                {{fastxlsx::CellValue::text("guard-empty-batch-noop-blocked")}});
        },
        PublicMaterializedGuardDiagnostic::ReplaceSheetData,
        "guard empty-batch no-op same-sheet replacement");

    data.set_cells(std::span<const fastxlsx::WorksheetCellUpdate>());
    data.append_row(std::span<const fastxlsx::CellValue>());
    data.set_cell_values(std::span<const fastxlsx::WorksheetCellUpdate>());
    data.set_row_values(5, std::span<const fastxlsx::CellValue>());
    data.set_column_values(5, std::span<const fastxlsx::CellValue>());
    data.clear_cell_values(std::span<const fastxlsx::WorksheetCellReference>());
    data.erase_cells(std::span<const fastxlsx::WorksheetCellReference>());

    check(!editor.last_edit_error().has_value(),
        "guard empty-batch no-ops should clear the same-sheet diagnostic");
    check(!data.has_pending_changes(),
        "guard empty-batch no-ops should keep Data clean");
    check(!editor.has_pending_changes(),
        "guard empty-batch no-ops should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "guard empty-batch no-ops should not queue public edits");
    check(editor.pending_worksheet_edits().empty(),
        "guard empty-batch no-ops should keep pending summaries empty");
    check(editor.pending_materialized_worksheet_names().empty(),
        "guard empty-batch no-ops should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "guard empty-batch no-ops should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "guard empty-batch no-ops should keep dirty materialized memory empty");
    check_public_preserved_sheet_diagnostics(
        data, data_cell_count, data_memory, "Data",
        "guard empty-batch no-ops");
    check(!data.try_cell(5, 5).has_value(),
        "guard empty-batch no-ops should not synthesize missing cells");
    check_public_inspection_preserves_last_edit_error(editor, std::nullopt);

    editor.save_as(output);
    check(!editor.last_edit_error().has_value(),
        "guard empty-batch no-op save_as should keep diagnostics clear");
    check(!data.has_pending_changes(),
        "guard empty-batch no-op save_as should keep Data clean");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "guard empty-batch no-op output should remain copy-original");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-empty-batch-noop-blocked",
        "guard empty-batch no-op rejected replacement should not leak into output");
}

void test_public_worksheet_editor_same_sheet_guard_missing_only_erase_noops_clear_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-guard-missing-only-erase-noops-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-guard-missing-only-erase-noops-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = data.get_cell("A1");
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "guard missing-only erase no-op setup should materialize Data from source");
    check(!data.has_pending_changes(),
        "guard missing-only erase no-op setup should keep Data clean");

    const std::size_t data_cell_count = data.cell_count();
    const std::size_t data_memory = data.estimated_memory_usage();
    check(!data.try_cell(5, 5).has_value() &&
            !data.try_cell("F6").has_value() &&
            !data.try_cell(7, 7).has_value(),
        "guard missing-only erase no-op setup should use absent erase targets");

    (void)check_public_same_sheet_guard_failure(
        editor,
        [&] {
            editor.replace_sheet_data("Data",
                {{fastxlsx::CellValue::text("guard-missing-only-erase-noop-blocked")}});
        },
        PublicMaterializedGuardDiagnostic::ReplaceSheetData,
        "guard missing-only erase no-op same-sheet replacement");

    data.erase_cells(fastxlsx::CellRange {5, 5, 5, 6});
    data.erase_cells("F6:G6");
    data.erase_cells({
        fastxlsx::WorksheetCellReference {7, 7},
        fastxlsx::WorksheetCellReference {7, 8},
        fastxlsx::WorksheetCellReference {7, 7},
    });

    check(!editor.last_edit_error().has_value(),
        "guard missing-only erase no-ops should clear the same-sheet diagnostic");
    check(!data.has_pending_changes(),
        "guard missing-only erase no-ops should keep Data clean");
    check(!editor.has_pending_changes(),
        "guard missing-only erase no-ops should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "guard missing-only erase no-ops should not queue public edits");
    check(editor.pending_worksheet_edits().empty(),
        "guard missing-only erase no-ops should keep pending summaries empty");
    check(editor.pending_materialized_worksheet_names().empty(),
        "guard missing-only erase no-ops should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "guard missing-only erase no-ops should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "guard missing-only erase no-ops should keep dirty materialized memory empty");
    check_public_preserved_sheet_diagnostics(
        data, data_cell_count, data_memory, "Data",
        "guard missing-only erase no-ops");
    check(!data.try_cell(5, 5).has_value() &&
            !data.try_cell(5, 6).has_value() &&
            !data.try_cell("F6").has_value() &&
            !data.try_cell("G6").has_value() &&
            !data.try_cell(7, 7).has_value() &&
            !data.try_cell(7, 8).has_value(),
        "guard missing-only erase no-ops should keep absent targets missing");
    check_public_inspection_preserves_last_edit_error(editor, std::nullopt);

    editor.save_as(output);
    check(!editor.last_edit_error().has_value(),
        "guard missing-only erase no-op save_as should keep diagnostics clear");
    check(!data.has_pending_changes(),
        "guard missing-only erase no-op save_as should keep Data clean");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "guard missing-only erase no-op output should remain copy-original");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-missing-only-erase-noop-blocked",
        "guard missing-only erase no-op rejected replacement should not leak into output");
}

void test_public_worksheet_editor_same_sheet_guard_missing_only_clear_noops_clear_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-guard-missing-only-clear-noops-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-guard-missing-only-clear-noops-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    const fastxlsx::CellValue source_value = data.get_cell("A1");
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a1",
        "guard missing-only clear no-op setup should materialize Data from source");
    check(!data.has_pending_changes(),
        "guard missing-only clear no-op setup should keep Data clean");

    const std::size_t data_cell_count = data.cell_count();
    const std::size_t data_memory = data.estimated_memory_usage();
    check(!data.try_cell(5, 5).has_value() &&
            !data.try_cell("F6").has_value() &&
            !data.try_cell(7, 7).has_value(),
        "guard missing-only clear no-op setup should use absent clear targets");

    (void)check_public_same_sheet_guard_failure(
        editor,
        [&] {
            editor.replace_sheet_data("Data",
                {{fastxlsx::CellValue::text("guard-missing-only-clear-noop-blocked")}});
        },
        PublicMaterializedGuardDiagnostic::ReplaceSheetData,
        "guard missing-only clear no-op same-sheet replacement");

    data.clear_cell_values(fastxlsx::CellRange {5, 5, 5, 6});
    data.clear_cell_values("F6:G6");
    data.clear_cell_values({
        fastxlsx::WorksheetCellReference {7, 7},
        fastxlsx::WorksheetCellReference {7, 8},
        fastxlsx::WorksheetCellReference {7, 7},
    });

    check(!editor.last_edit_error().has_value(),
        "guard missing-only clear no-ops should clear the same-sheet diagnostic");
    check(!data.has_pending_changes(),
        "guard missing-only clear no-ops should keep Data clean");
    check(!editor.has_pending_changes(),
        "guard missing-only clear no-ops should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "guard missing-only clear no-ops should not queue public edits");
    check(editor.pending_worksheet_edits().empty(),
        "guard missing-only clear no-ops should keep pending summaries empty");
    check(editor.pending_materialized_worksheet_names().empty(),
        "guard missing-only clear no-ops should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "guard missing-only clear no-ops should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "guard missing-only clear no-ops should keep dirty materialized memory empty");
    check_public_preserved_sheet_diagnostics(
        data, data_cell_count, data_memory, "Data",
        "guard missing-only clear no-ops");
    check(!data.try_cell(5, 5).has_value() &&
            !data.try_cell(5, 6).has_value() &&
            !data.try_cell("F6").has_value() &&
            !data.try_cell("G6").has_value() &&
            !data.try_cell(7, 7).has_value() &&
            !data.try_cell(7, 8).has_value(),
        "guard missing-only clear no-ops should keep absent targets missing");
    check_public_inspection_preserves_last_edit_error(editor, std::nullopt);

    editor.save_as(output);
    check(!editor.last_edit_error().has_value(),
        "guard missing-only clear no-op save_as should keep diagnostics clear");
    check(!data.has_pending_changes(),
        "guard missing-only clear no-op save_as should keep Data clean");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "guard missing-only clear no-op output should remain copy-original");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "guard-missing-only-clear-noop-blocked",
        "guard missing-only clear no-op rejected replacement should not leak into output");
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

void test_public_worksheet_editor_clean_same_sheet_failure_then_noop_clear_clears_diagnostic()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-failure-noop-clear-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-failure-noop-clear-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        const fastxlsx::CellValue source_value = data.get_cell(1, 1);
        check(source_value.kind() == fastxlsx::CellValueKind::Text &&
                source_value.text_value() == "placeholder-a1",
            "read-only no-op clear recovery setup should materialize Data from source");
        check(!data.has_pending_changes(),
            "read-only no-op clear recovery setup should keep Data clean");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        check(!data.try_cell(5, 5).has_value(),
            "read-only no-op clear recovery setup should use a missing target cell");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-blocked-before-noop-clear")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only same-sheet replacement failure before no-op clear");

        data.clear_cell_value(5, 5);

        check(!editor.last_edit_error().has_value(),
            "read-only successful no-op clear should clear prior same-sheet diagnostic");
        check(!data.has_pending_changes(),
            "read-only successful no-op clear should keep Data clean");
        check(!editor.has_pending_changes(),
            "read-only successful no-op clear should keep WorkbookEditor clean");
        check(editor.pending_change_count() == 0,
            "read-only successful no-op clear should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only successful no-op clear should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only successful no-op clear should keep dirty materialized cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "read-only successful no-op clear should keep dirty materialized memory empty");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "read-only successful no-op clear");
        check(!data.try_cell(5, 5).has_value(),
            "read-only successful no-op clear should keep the missing target absent");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "read-only no-op clear recovery save_as should keep last_edit_error clear");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "read-only no-op clear recovery output should remain copy-original");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "readonly-blocked-before-noop-clear",
            "read-only rejected replacement should not leak after no-op clear recovery");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-noop-clear-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-noop-clear-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-failure-noop-clear-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-noop-clear-data"));
        editor.save_as(first_output);
        const auto first_output_entries =
            fastxlsx::test::read_zip_entries(first_output);

        check(!data.has_pending_changes(),
            "saved-clean no-op clear recovery setup should leave Data clean");
        const std::size_t saved_pending_count = editor.pending_change_count();
        check(saved_pending_count == 1,
            "saved-clean no-op clear recovery setup should retain one materialized handoff");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        check(!data.try_cell("E5").has_value(),
            "saved-clean no-op clear recovery setup should use a missing A1 target cell");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanBlockedBeforeNoopClear");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean same-sheet rename failure before no-op clear");

        data.clear_cell_value("E5");

        check(!editor.last_edit_error().has_value(),
            "saved-clean successful no-op clear should clear prior same-sheet diagnostic");
        check(!data.has_pending_changes(),
            "saved-clean successful no-op clear should keep Data clean");
        check(editor.pending_change_count() == saved_pending_count,
            "saved-clean successful no-op clear should preserve saved handoff count");
        check(editor.pending_materialized_worksheet_names().empty(),
            "saved-clean successful no-op clear should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "saved-clean successful no-op clear should keep dirty materialized cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "saved-clean successful no-op clear should keep dirty materialized memory empty");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "saved-clean successful no-op clear");
        check(!data.try_cell("E5").has_value(),
            "saved-clean successful no-op clear should keep the missing target absent");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "saved-clean no-op clear recovery save_as should keep last_edit_error clear");
        check(!data.has_pending_changes(),
            "saved-clean no-op clear recovery save_as should leave Data clean");
        check(editor.pending_change_count() == saved_pending_count,
            "saved-clean no-op clear recovery save_as should not add a materialized handoff");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == first_output_entries,
            "saved-clean no-op clear recovery output should match the first saved output");
        check_not_contains(output_entries.at("xl/workbook.xml"),
            "SavedCleanBlockedBeforeNoopClear",
            "saved-clean rejected rename should not leak after no-op clear recovery");
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

void test_public_worksheet_editor_noop_clear_recovery_preserves_same_sheet_patch_guard()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-noop-clear-guard-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-noop-clear-guard-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        const fastxlsx::CellValue source_value = data.get_cell(1, 1);
        check(source_value.kind() == fastxlsx::CellValueKind::Text &&
                source_value.text_value() == "placeholder-a1",
            "read-only no-op clear guard setup should materialize Data from source");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        check(!data.try_cell(5, 5).has_value(),
            "read-only no-op clear guard setup should use a missing clear target");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-blocked-before-noop-clear-guard")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only initial replacement failure before no-op clear guard");

        data.clear_cell_value(5, 5);
        check(!editor.last_edit_error().has_value(),
            "read-only no-op clear should clear initial same-sheet diagnostic");
        check(!data.has_pending_changes(),
            "read-only no-op clear should keep Data clean before second same-sheet Patch");

        const std::optional<std::string> rename_error =
            check_public_same_sheet_guard_failure(
                editor,
                [&] {
                    editor.rename_sheet("Data", "ReadonlyNoopClearBypass");
                },
                PublicMaterializedGuardDiagnostic::RenameSheet,
                "read-only same-sheet rename after no-op clear",
                PublicMaterializedGuardDiagnostic::ReplaceSheetData);

        check(!data.has_pending_changes(),
            "read-only second same-sheet failure after no-op clear should keep Data clean");
        check(!editor.has_pending_changes(),
            "read-only second same-sheet failure after no-op clear should keep WorkbookEditor clean");
        check(editor.pending_change_count() == 0,
            "read-only second same-sheet failure after no-op clear should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only second same-sheet failure after no-op clear should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only second same-sheet failure after no-op clear should keep dirty materialized cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "read-only second same-sheet failure after no-op clear should keep dirty materialized memory empty");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "read-only second same-sheet failure after no-op clear");
        check_public_inspection_preserves_last_edit_error(editor, rename_error);

        editor.save_as(output);
        check(editor.last_edit_error() == rename_error,
            "read-only no-op clear guard save_as should preserve the latest same-sheet diagnostic");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "read-only no-op clear guard output should remain copy-original");
        check_not_contains(output_entries.at("xl/workbook.xml"),
            "ReadonlyNoopClearBypass",
            "read-only rejected rename after no-op clear should not leak into workbook catalog");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "readonly-blocked-before-noop-clear-guard",
            "read-only rejected replacement before no-op clear should not leak into Data");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-noop-clear-guard-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-noop-clear-guard-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-noop-clear-guard-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-noop-clear-guard-data"));
        editor.save_as(first_output);
        const auto first_output_entries =
            fastxlsx::test::read_zip_entries(first_output);

        check(!data.has_pending_changes(),
            "saved-clean no-op clear guard setup should leave Data clean");
        const std::size_t saved_pending_count = editor.pending_change_count();
        check(saved_pending_count == 1,
            "saved-clean no-op clear guard setup should retain one materialized handoff");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        check(!data.try_cell("E5").has_value(),
            "saved-clean no-op clear guard setup should use a missing A1 clear target");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanNoopClearBypass");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean initial rename failure before no-op clear guard");

        data.clear_cell_value("E5");
        check(!editor.last_edit_error().has_value(),
            "saved-clean no-op clear should clear initial same-sheet diagnostic");
        check(!data.has_pending_changes(),
            "saved-clean no-op clear should keep Data clean before second same-sheet Patch");

        const std::optional<std::string> replacement_error =
            check_public_same_sheet_guard_failure(
                editor,
                [&] {
                    editor.replace_sheet_data("Data",
                        {{fastxlsx::CellValue::text("saved-clean-blocked-after-noop-clear-guard")}});
                },
                PublicMaterializedGuardDiagnostic::ReplaceSheetData,
                "saved-clean same-sheet replacement after no-op clear",
                PublicMaterializedGuardDiagnostic::RenameSheet);

        check(!data.has_pending_changes(),
            "saved-clean second same-sheet failure after no-op clear should keep Data clean");
        check(editor.pending_change_count() == saved_pending_count,
            "saved-clean second same-sheet failure after no-op clear should preserve saved handoff count");
        check(editor.pending_materialized_worksheet_names().empty(),
            "saved-clean second same-sheet failure after no-op clear should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "saved-clean second same-sheet failure after no-op clear should keep dirty materialized cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "saved-clean second same-sheet failure after no-op clear should keep dirty materialized memory empty");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "saved-clean second same-sheet failure after no-op clear");
        check_public_inspection_preserves_last_edit_error(editor, replacement_error);

        editor.save_as(output);
        check(editor.last_edit_error() == replacement_error,
            "saved-clean no-op clear guard save_as should preserve the latest same-sheet diagnostic");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == first_output_entries,
            "saved-clean no-op clear guard output should match the first saved output");
        check_not_contains(output_entries.at("xl/workbook.xml"),
            "SavedCleanNoopClearBypass",
            "saved-clean rejected rename before no-op clear should not leak into workbook catalog");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "saved-clean-blocked-after-noop-clear-guard",
            "saved-clean rejected replacement after no-op clear should not leak into Data");
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

void test_public_worksheet_editor_two_clean_noop_clear_preserves_other_guard()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-clear-guard-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-clear-guard-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        const fastxlsx::CellValue data_value = data.get_cell(1, 1);
        const fastxlsx::CellValue untouched_value = untouched.get_cell(1, 1);
        check(data_value.kind() == fastxlsx::CellValueKind::Text &&
                data_value.text_value() == "placeholder-a1",
            "read-only two-clean clear guard setup should materialize Data");
        check(untouched_value.kind() == fastxlsx::CellValueKind::Text &&
                untouched_value.text_value() == "keep-me",
            "read-only two-clean clear guard setup should materialize Untouched");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        const std::size_t untouched_cell_count = untouched.cell_count();
        const std::size_t untouched_memory = untouched.estimated_memory_usage();
        check(!data.has_pending_changes() && !untouched.has_pending_changes(),
            "read-only two-clean clear guard setup should keep both handles clean");
        check(!data.try_cell(5, 5).has_value(),
            "read-only two-clean clear guard setup should use a missing Data clear target");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-two-clean-clear-blocked-data")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only two-clean Data failure before no-op clear");

        data.clear_cell_value(5, 5);
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean Data no-op clear should clear Data diagnostic");
        check(!data.has_pending_changes(),
            "read-only two-clean Data no-op clear should keep Data clean");
        check(!untouched.has_pending_changes(),
            "read-only two-clean Data no-op clear should not dirty Untouched");

        const std::optional<std::string> untouched_error =
            check_public_same_sheet_guard_failure(
                editor,
                [&] {
                    editor.replace_sheet_data("Untouched",
                        {{fastxlsx::CellValue::text("readonly-two-clean-clear-blocked-untouched")}});
                },
                PublicMaterializedGuardDiagnostic::ReplaceSheetData,
                "read-only two-clean Untouched failure after Data no-op clear");
        if (untouched_error.has_value()) {
            check_not_contains(*untouched_error, "Data",
                "read-only two-clean clear Untouched guard should not retain the Data diagnostic context");
        }

        check_public_two_clean_preserved_clean_handles_state(
            editor, data, untouched, data_cell_count, data_memory,
            untouched_cell_count, untouched_memory, 0, true,
            "read-only two-clean second failure after Data no-op clear");

        editor.save_as(output);
        check(editor.last_edit_error() == untouched_error,
            "read-only two-clean clear guard save_as should preserve the latest Untouched diagnostic");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_recovery_copy_original_output(
            output_entries, source_entries, "readonly-two-clean-clear-blocked-data",
            "readonly-two-clean-clear-blocked-untouched",
            "read-only two-clean clear guard output");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-clear-guard-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-clear-guard-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-clear-guard-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clear-guard-data"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clear-guard-untouched"));
        editor.save_as(first_output);

        const std::size_t saved_pending_count =
            check_public_two_clean_two_handle_clean_state(
                editor, data, untouched, 2,
                "saved-clean two-clean clear guard setup");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        const std::size_t untouched_cell_count = untouched.cell_count();
        const std::size_t untouched_memory = untouched.estimated_memory_usage();
        check(!data.try_cell("E5").has_value(),
            "saved-clean two-clean clear guard setup should use a missing Data A1 clear target");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanTwoClearGuardBlockedData");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean two-clean Data failure before no-op clear");

        data.clear_cell_value("E5");
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean Data no-op clear should clear Data diagnostic");
        check(!data.has_pending_changes(),
            "saved-clean two-clean Data no-op clear should keep Data clean");
        check(!untouched.has_pending_changes(),
            "saved-clean two-clean Data no-op clear should keep Untouched clean");

        const std::optional<std::string> untouched_error =
            check_public_same_sheet_guard_failure(
                editor,
                [&] {
                    editor.replace_sheet_data("Untouched",
                        {{fastxlsx::CellValue::text("saved-clean-two-clear-guard-blocked-untouched")}});
                },
                PublicMaterializedGuardDiagnostic::ReplaceSheetData,
                "saved-clean two-clean Untouched failure after Data no-op clear",
                PublicMaterializedGuardDiagnostic::RenameSheet);

        check_public_two_clean_preserved_clean_handles_state(
            editor, data, untouched, data_cell_count, data_memory,
            untouched_cell_count, untouched_memory, saved_pending_count, false,
            "saved-clean two-clean second failure after Data no-op clear");
        check_public_inspection_preserves_last_edit_error(editor, untouched_error);

        editor.save_as(output);
        check(editor.last_edit_error() == untouched_error,
            "saved-clean two-clean clear guard save_as should preserve the latest Untouched diagnostic");
        check_public_two_clean_two_handle_clean_state(
            editor, data, untouched, saved_pending_count,
            "saved-clean two-clean clear guard save_as");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_saved_clean_output(
            output_entries, "SavedCleanTwoClearGuardBlockedData",
            "saved-clean-two-clear-guard-data",
            "saved-clean-two-clear-guard-untouched",
            "saved-clean two-clean clear guard output",
            "saved-clean-two-clear-guard-blocked-untouched");
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

void test_public_worksheet_editor_two_clean_noop_clear_allows_scoped_other_mutation()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-clear-other-mutation-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-readonly-two-clean-clear-other-mutation-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        const fastxlsx::CellValue data_value = data.get_cell(1, 1);
        const fastxlsx::CellValue untouched_value = untouched.get_cell(1, 1);
        check(data_value.kind() == fastxlsx::CellValueKind::Text &&
                data_value.text_value() == "placeholder-a1",
            "read-only two-clean clear other-mutation setup should materialize Data");
        check(untouched_value.kind() == fastxlsx::CellValueKind::Text &&
                untouched_value.text_value() == "keep-me",
            "read-only two-clean clear other-mutation setup should materialize Untouched");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        const std::size_t untouched_cell_count = untouched.cell_count();
        check(!data.has_pending_changes() && !untouched.has_pending_changes(),
            "read-only two-clean clear other-mutation setup should keep both handles clean");
        check(!data.try_cell(5, 5).has_value(),
            "read-only two-clean clear other-mutation setup should use a missing Data clear target");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.replace_sheet_data("Data",
                    {{fastxlsx::CellValue::text("readonly-two-clean-clear-other-mutation-blocked-data")}});
            },
            PublicMaterializedGuardDiagnostic::ReplaceSheetData,
            "read-only two-clean Data failure before no-op clear other mutation");

        data.clear_cell_value(5, 5);
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean Data no-op clear should clear diagnostic before other mutation");
        check(!data.has_pending_changes(),
            "read-only two-clean Data no-op clear should keep Data clean before other mutation");

        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("readonly-two-clean-clear-untouched-mutated"));
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean clear Untouched mutation should keep last_edit_error clear");
        check_public_two_clean_single_dirty_materialized_state(
            editor, untouched, data, "Untouched", untouched_cell_count + 1, 0,
            "read-only two-clean clear Untouched mutation");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "read-only two-clean clear Untouched mutation");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "read-only two-clean clear other-mutation save_as should keep last_edit_error clear");
        check_public_two_clean_two_handle_clean_state(
            editor, data, untouched, 1,
            "read-only two-clean clear other-mutation save_as");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_other_mutation_readonly_output(
            output_entries, source_entries, "readonly-two-clean-clear-other-mutation-blocked-data",
            "readonly-two-clean-clear-untouched-mutated",
            "read-only two-clean clear other-mutation output");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-clear-other-mutation-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-clear-other-mutation-first.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-saved-clean-two-clean-clear-other-mutation-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor data = editor.worksheet("Data");
        fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
        data.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clear-other-mutation-data"));
        untouched.set_cell(1, 1,
            fastxlsx::CellValue::text("saved-clean-two-clear-other-mutation-untouched"));
        editor.save_as(first_output);

        const std::size_t saved_pending_count =
            check_public_two_clean_two_handle_clean_state(
                editor, data, untouched, 2,
                "saved-clean two-clean clear other-mutation setup");
        const std::size_t data_cell_count = data.cell_count();
        const std::size_t data_memory = data.estimated_memory_usage();
        const std::size_t untouched_cell_count = untouched.cell_count();
        check(!data.try_cell("E5").has_value(),
            "saved-clean two-clean clear other-mutation setup should use a missing Data A1 clear target");

        (void)check_public_same_sheet_guard_failure(
            editor,
            [&] {
                editor.rename_sheet("Data", "SavedCleanTwoClearOtherMutationBlockedData");
            },
            PublicMaterializedGuardDiagnostic::RenameSheet,
            "saved-clean two-clean Data failure before no-op clear other mutation");

        data.clear_cell_value("E5");
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean Data no-op clear should clear diagnostic before other mutation");
        check(!data.has_pending_changes(),
            "saved-clean two-clean Data no-op clear should keep Data clean before other mutation");

        untouched.set_cell(2, 2,
            fastxlsx::CellValue::text("saved-clean-two-clear-untouched-after-data-noop"));
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean clear Untouched mutation should keep last_edit_error clear");
        check_public_two_clean_single_dirty_materialized_state(
            editor, untouched, data, "Untouched", untouched_cell_count + 1,
            saved_pending_count, "saved-clean two-clean clear Untouched mutation");
        check_public_preserved_sheet_diagnostics(
            data, data_cell_count, data_memory, "Data",
            "saved-clean two-clean clear Untouched mutation");

        editor.save_as(output);
        check(!editor.last_edit_error().has_value(),
            "saved-clean two-clean clear other-mutation save_as should keep last_edit_error clear");
        check_public_two_clean_two_handle_clean_state(
            editor, data, untouched, saved_pending_count + 1,
            "saved-clean two-clean clear other-mutation save_as");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_public_two_clean_saved_clean_output(
            output_entries, "SavedCleanTwoClearOtherMutationBlockedData",
            "saved-clean-two-clear-other-mutation-data",
            "saved-clean-two-clear-untouched-after-data-noop",
            "saved-clean two-clean clear other-mutation output");
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

        if (should_run_workbook_editor_shard(shard, "public-guards")) {
            test_public_worksheet_editor_blocks_same_sheet_patch_operations();
            test_public_worksheet_editor_readonly_session_blocks_same_sheet_patch_operations();
            test_public_worksheet_editor_saved_clean_session_blocks_same_sheet_patch_operations();
            test_public_worksheet_editor_clean_sessions_allow_cross_sheet_patch_operations();
            test_public_worksheet_editor_clean_same_sheet_patch_failures_replace_diagnostics();
            test_public_worksheet_editor_same_sheet_guard_snapshot_reads_preserve_diagnostic();
            test_public_worksheet_editor_same_sheet_guard_scalar_reads_preserve_diagnostic();
            test_public_worksheet_editor_same_sheet_guard_invalid_reads_preserve_diagnostic();
            test_public_worksheet_editor_same_sheet_guard_invalid_mutations_replace_diagnostic();
            test_public_worksheet_editor_same_sheet_guard_invalid_reads_preserve_invalid_mutation_diagnostic();
            test_public_worksheet_editor_same_sheet_guard_empty_batch_noops_clear_diagnostic();
            test_public_worksheet_editor_same_sheet_guard_missing_only_erase_noops_clear_diagnostic();
            test_public_worksheet_editor_same_sheet_guard_missing_only_clear_noops_clear_diagnostic();
            test_public_worksheet_editor_clean_same_sheet_failure_then_cross_sheet_success_clears_diagnostic();
            test_public_worksheet_editor_clean_same_sheet_failure_then_worksheet_mutation_clears_diagnostic();
            test_public_worksheet_editor_clean_same_sheet_failure_then_noop_erase_clears_diagnostic();
            test_public_worksheet_editor_clean_same_sheet_failure_then_noop_clear_clears_diagnostic();
            test_public_worksheet_editor_noop_erase_recovery_preserves_same_sheet_patch_guard();
            test_public_worksheet_editor_noop_clear_recovery_preserves_same_sheet_patch_guard();
            test_public_worksheet_editor_recovery_with_two_clean_handles_preserves_other_guard();
            test_public_worksheet_editor_two_clean_noop_clear_preserves_other_guard();
            test_public_worksheet_editor_recovery_with_two_clean_handles_allows_scoped_other_mutation();
            test_public_worksheet_editor_two_clean_noop_clear_allows_scoped_other_mutation();
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
