// WorkbookEditor source materialization failure structure tests.
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
        || shard == "source-failure"
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
            || left.freeze_panes_changed != right.freeze_panes_changed
            || left.frozen_row_count != right.frozen_row_count
            || left.frozen_column_count != right.frozen_column_count
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
    std::size_t pending_targeted_cell_replacement_count{};
    std::vector<std::string> pending_targeted_cell_replacement_worksheet_names;
    std::size_t estimated_pending_targeted_cell_replacement_xml_bytes{};
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
        editor.pending_targeted_cell_replacement_count(),
        editor.pending_targeted_cell_replacement_worksheet_names(),
        editor.estimated_pending_targeted_cell_replacement_xml_bytes(),
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
    const std::vector<fastxlsx::WorksheetCellUpdate> invalid_batch = {
        {fastxlsx::WorksheetCellReference {1, 1},
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-batch-valid-before-failure")},
        {fastxlsx::WorksheetCellReference {0, 1},
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-batch-row-zero")}};
    check(threw_fastxlsx_error([&] { data_again.set_cells(invalid_batch); }),
        label + " should reject row-zero set_cells without applying earlier batch updates");
    const std::vector<fastxlsx::WorksheetCellUpdate> invalid_value_batch = {
        {fastxlsx::WorksheetCellReference {1, 1},
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-value-batch-valid-before-failure")},
        {fastxlsx::WorksheetCellReference {1, 0},
            public_two_clean_retry_rejected_mutation_value(
                rejected_prefix, "-value-batch-column-zero")}};
    check(threw_fastxlsx_error([&] { data_again.set_cell_values(invalid_value_batch); }),
        label + " should reject column-zero set_cell_values without applying earlier batch updates");
    check(threw_fastxlsx_error([&] { data.clear_cell_value(0, 1); }),
        label + " should reject row-zero clear_cell_value");
    check(threw_fastxlsx_error([&] { untouched.clear_cell_value("XFE1"); }),
        label + " should reject A1 column-overflow clear_cell_value");
    check(threw_fastxlsx_error([&] {
        data_again.clear_cell_values(fastxlsx::CellRange {0, 1, 1, 1});
    }), label + " should reject row-zero clear_cell_values");
    check(threw_fastxlsx_error([&] {
        untouched_again.clear_cell_values(fastxlsx::CellRange {2, 1, 1, 1});
    }), label + " should reject reversed clear_cell_values");
    check(threw_fastxlsx_error([&] {
        data_again.erase_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), label + " should reject row-zero erase_cells range");
    check(threw_fastxlsx_error([&] {
        untouched_again.erase_cells(fastxlsx::CellRange {2, 1, 1, 1});
    }), label + " should reject reversed erase_cells range");
    const std::vector<fastxlsx::WorksheetCellReference> invalid_clear_batch = {
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1048577, 1}};
    check(threw_fastxlsx_error([&] { data.clear_cell_values(invalid_clear_batch); }),
        label + " should reject row-overflow coordinate clear_cell_values");
    const std::vector<fastxlsx::WorksheetCellReference> invalid_erase_batch = {
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 16385}};
    check(threw_fastxlsx_error([&] { untouched.erase_cells(invalid_erase_batch); }),
        label + " should reject column-overflow coordinate erase_cells");
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
    std::string_view target_sheet_name = "Data",
    bool check_clean_noop_recovery = false)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    std::map<std::string, std::string> source_entries;
    if (check_clean_noop_recovery) {
        source_entries = fastxlsx::test::read_zip_entries(source);
    }

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
    if (check_clean_noop_recovery) {
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            std::string(scenario) + " try_worksheet failure should not mutate the source package");
    }

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
    if (check_clean_noop_recovery) {
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            std::string(scenario) + " worksheet failure should not mutate the source package");
    }

    editor.replace_sheet_data(std::string(recovery_sheet_name),
        {{fastxlsx::CellValue::text(std::string(replacement_text))}});
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    if (check_clean_noop_recovery) {
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            std::string(scenario) + " recovery save_as should not mutate the source package");
    }
    check_contains(output_entries.at(std::string(output_entry_name)), replacement_text,
        std::string(scenario) + " editor should remain usable after materialization failure");
    if (check_clean_noop_recovery) {
        const std::filesystem::path noop_output =
            output.parent_path()
            / (output.stem().string() + "-noop" + output.extension().string());
        editor.save_as(noop_output);
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            std::string(scenario) + " clean no-op save_as after recovery should be byte-stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            std::string(scenario) + " clean no-op save_as after recovery should not mutate the source package");
    }
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

void test_public_worksheet_editor_failed_materialization_keeps_noop_save_as_copy_original()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-failed-materialize-noop-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-failed-materialize-noop-output.xlsx");

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1" s="+1" t="inlineStr"><is><t>invalid-style-source</t></is></c>)"
        R"(</row></sheetData>)"
        R"(</worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.has_worksheet("Data"),
        "failed materialization no-op should preserve planned sheet catalog");
    check(editor.has_source_worksheet("Data"),
        "failed materialization no-op should preserve source sheet catalog");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();

    bool try_failed = false;
    try {
        (void)editor.try_worksheet("Data");
    } catch (const fastxlsx::FastXlsxError& error) {
        try_failed = true;
        check_contains(error.what(), "invalid style id reference",
            "try_worksheet should expose failed materialization diagnostic");
    }
    check(try_failed,
        "try_worksheet should fail when source materialization rejects invalid style ids");
    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        "failed materialization no-op",
        "try_worksheet failure",
        "Untouched");

    bool worksheet_failed = false;
    try {
        (void)editor.worksheet("Data");
    } catch (const fastxlsx::FastXlsxError& error) {
        worksheet_failed = true;
        check_contains(error.what(), "invalid style id reference",
            "worksheet should expose failed materialization diagnostic");
    }
    check(worksheet_failed,
        "worksheet should fail when source materialization rejects invalid style ids");
    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        "failed materialization no-op",
        "worksheet failure",
        "Untouched");

    editor.save_as(output);

    check_public_materialization_failure_clean_state(
        editor,
        expected_source_names,
        expected_planned_names,
        expected_catalog,
        "failed materialization no-op",
        "save_as copy-original",
        "Untouched");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as after failed materialization should copy source entries");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(s="+1")",
        "no-op save_as after failed materialization should preserve rejected invalid source style id");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "invalid-style-source",
        "no-op save_as after failed materialization should preserve rejected source worksheet bytes");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "no-op save_as after failed materialization should preserve untouched sheets");

    fastxlsx::WorkbookEditor copied_editor = fastxlsx::WorkbookEditor::open(output);
    check(copied_editor.has_worksheet("Data"),
        "copy-original output after failed materialization should preserve planned catalog");
    check(copied_editor.has_source_worksheet("Data"),
        "copy-original output after failed materialization should preserve source catalog");
    const std::vector<std::string> copied_source_names =
        copied_editor.source_worksheet_names();
    const std::vector<std::string> copied_planned_names =
        copied_editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> copied_catalog =
        copied_editor.worksheet_catalog();

    bool copied_try_failed = false;
    try {
        (void)copied_editor.try_worksheet("Data");
    } catch (const fastxlsx::FastXlsxError& error) {
        copied_try_failed = true;
        check_contains(error.what(), "invalid style id reference",
            "copy-original output try_worksheet should preserve materialization diagnostic");
    }
    check(copied_try_failed,
        "copy-original output try_worksheet should still reject the invalid source style id");
    check_public_materialization_failure_clean_state(
        copied_editor,
        copied_source_names,
        copied_planned_names,
        copied_catalog,
        "failed materialization no-op copied output",
        "try_worksheet failure",
        "Untouched");

    bool copied_worksheet_failed = false;
    try {
        (void)copied_editor.worksheet("Data");
    } catch (const fastxlsx::FastXlsxError& error) {
        copied_worksheet_failed = true;
        check_contains(error.what(), "invalid style id reference",
            "copy-original output worksheet should preserve materialization diagnostic");
    }
    check(copied_worksheet_failed,
        "copy-original output worksheet should still reject the invalid source style id");
    check_public_materialization_failure_clean_state(
        copied_editor,
        copied_source_names,
        copied_planned_names,
        copied_catalog,
        "failed materialization no-op copied output",
        "worksheet failure",
        "Untouched");
}

void test_public_worksheet_editor_rejects_invalid_source_shared_string_index()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-invalid-sharedstring-index-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-invalid-sharedstring-index-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("shared")});
        writer.close();
    }

    std::string worksheet_xml =
        fastxlsx::test::read_zip_entries(source).at("xl/worksheets/sheet1.xml");
    replace_first_or_throw(worksheet_xml, "<v>0</v>", "<v>99</v>");
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);

    check_public_worksheet_materialization_failure_hygiene(source, output,
        "CellStore worksheet loader found a shared string index out of range",
        "usable-after-failure",
        "out-of-range source shared string index",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Data",
        true);
}

void test_public_worksheet_editor_rejects_invalid_source_shared_strings_metadata()
{
    const auto write_shared_string_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("shared-public-metadata")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-public-metadata")});
        writer.close();
        return source;
    };

    const auto expect_public_materialization_failure =
        [&](std::string_view tag,
            const std::function<void(std::map<std::string, std::string>&)>& mutate_entries,
            std::string_view expected_diagnostic,
            std::string_view scenario) {
            const std::filesystem::path source = write_shared_string_source(
                std::string("fastxlsx-workbook-editor-public-invalid-sharedstrings-")
                + std::string(tag) + "-source.xlsx");
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-invalid-sharedstrings-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            mutate_entries(entries);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(
                source, output, expected_diagnostic, replacement_text, scenario);
        };

    expect_public_materialization_failure(
        "duplicate-rel",
        [](std::map<std::string, std::string>& entries) {
            std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
            replace_first_or_throw(workbook_relationships,
                R"(</Relationships>)",
                R"(<Relationship Id="rId99" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
                R"(</Relationships>)");
        },
        "workbook sharedStrings lookup found multiple sharedStrings relationships",
        "duplicate sharedStrings relationship");

    expect_public_materialization_failure(
        "missing-target",
        [](std::map<std::string, std::string>& entries) {
            std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
            replace_first_or_throw(
                workbook_relationships, R"(Target="sharedStrings.xml")",
                R"(Target="missingSharedStrings.xml")");
        },
        "workbook sharedStrings relationship targets an unknown package part",
        "missing sharedStrings target");

    expect_public_materialization_failure(
        "external-target",
        [](std::map<std::string, std::string>& entries) {
            std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
            replace_first_or_throw(workbook_relationships,
                R"(Target="sharedStrings.xml")",
                R"(Target="https://example.invalid/sharedStrings.xml" TargetMode="External")");
        },
        "sharedStrings relationship target cannot be external",
        "external sharedStrings target");

    expect_public_materialization_failure(
        "query-target",
        [](std::map<std::string, std::string>& entries) {
            std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
            replace_first_or_throw(workbook_relationships,
                R"(Target="sharedStrings.xml")",
                R"(Target="sharedStrings.xml?version=1")");
        },
        "sharedStrings relationship target must be a package part",
        "query-qualified sharedStrings target");

    expect_public_materialization_failure(
        "fragment-target",
        [](std::map<std::string, std::string>& entries) {
            std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
            replace_first_or_throw(workbook_relationships,
                R"(Target="sharedStrings.xml")",
                R"(Target="sharedStrings.xml#frag")");
        },
        "sharedStrings relationship target must be a package part",
        "fragment-qualified sharedStrings target");

    expect_public_materialization_failure(
        "incomplete-percent-target",
        [](std::map<std::string, std::string>& entries) {
            std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
            replace_first_or_throw(workbook_relationships,
                R"(Target="sharedStrings.xml")",
                R"(Target="sharedStrings%2")");
        },
        "relationship target percent escape is incomplete",
        "sharedStrings target with incomplete percent escape");

    expect_public_materialization_failure(
        "invalid-percent-target",
        [](std::map<std::string, std::string>& entries) {
            std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
            replace_first_or_throw(workbook_relationships,
                R"(Target="sharedStrings.xml")",
                R"(Target="sharedStrings%ZZ.xml")");
        },
        "relationship target percent escape is invalid",
        "sharedStrings target with invalid percent escape");

    expect_public_materialization_failure(
        "null-percent-target",
        [](std::map<std::string, std::string>& entries) {
            std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
            replace_first_or_throw(workbook_relationships,
                R"(Target="sharedStrings.xml")",
                R"(Target="sharedStrings%00.xml")");
        },
        "relationship target cannot contain null bytes",
        "sharedStrings target with decoded null byte");

    expect_public_materialization_failure(
        "root-escape-target",
        [](std::map<std::string, std::string>& entries) {
            std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
            replace_first_or_throw(workbook_relationships,
                R"(Target="sharedStrings.xml")",
                R"(Target="../../sharedStrings.xml")");
        },
        "part name cannot escape the package root",
        "sharedStrings target escaping package root");

    expect_public_materialization_failure(
        "wrong-content-type",
        [](std::map<std::string, std::string>& entries) {
            std::string& content_types = entries.at("[Content_Types].xml");
            replace_first_or_throw(content_types,
                "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
                "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
        },
        "workbook sharedStrings relationship target is not a sharedStrings part",
        "wrong sharedStrings content type");

    expect_public_materialization_failure(
        "malformed-xml",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") = R"(<notSst/>)";
        },
        "CellStore sharedStrings loader root is missing an sst element",
        "malformed sharedStrings XML");

    expect_public_materialization_failure(
        "wrong-namespace-unsupported-item",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<bad:sst xmlns:bad="urn:fastxlsx:not-spreadsheetml" count="1" uniqueCount="1">)"
                R"(<bad:si><bad:unsupportedItem><bad:t>bad</bad:t></bad:unsupportedItem></bad:si>)"
                R"(</bad:sst>)";
        },
        "CellStore sharedStrings loader found an unsupported shared string item element",
        "wrong-namespace unsupported sharedStrings item local-name");

    expect_public_materialization_failure(
        "wrong-namespace-unsupported-rich-run",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<bad:sst xmlns:bad="urn:fastxlsx:not-spreadsheetml" count="1" uniqueCount="1">)"
                R"(<bad:si><bad:r><bad:unsupportedRun><bad:t>bad</bad:t></bad:unsupportedRun></bad:r></bad:si>)"
                R"(</bad:sst>)";
        },
        "CellStore sharedStrings loader found an unsupported shared string rich text element",
        "wrong-namespace unsupported sharedStrings rich-run local-name");

    expect_public_materialization_failure(
        "mixed-direct-rich",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>direct</t><r><t>rich</t></r></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found mixed direct and rich text in a shared string item",
        "sharedStrings item mixing direct and rich text");

    expect_public_materialization_failure(
        "rpr-outside-run",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><rPr><b/></rPr></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed rich text metadata",
        "sharedStrings run properties outside a rich run");

    expect_public_materialization_failure(
        "text-inside-rpr",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><r><rPr><t>not-text</t></rPr><t>rich</t></r></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed rich text metadata",
        "sharedStrings text inside rich run properties");

    expect_public_materialization_failure(
        "ignored-metadata-nested-si",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><rPh sb="0" eb="1"><si><t>decoy</t></si></rPh><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found a nested shared string item",
        "sharedStrings ignored metadata with nested si decoy");

    expect_public_materialization_failure(
        "ignored-metadata-nested-markup-in-text",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><rPh sb="0" eb="1"><t>ignored<r/>text</t></rPh><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found nested markup inside a text element",
        "sharedStrings ignored metadata with nested markup inside text");

    expect_public_materialization_failure(
        "ignored-metadata-orphan-closing",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>bad</t></rPh></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found mismatched closing tags",
        "sharedStrings ignored metadata orphan closing tag");

    expect_public_materialization_failure(
        "ignored-metadata-unclosed",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><rPh sb="0" eb="1"><t>ignored</t>)";
        },
        "CellStore sharedStrings loader found malformed XML",
        "sharedStrings ignored metadata left unclosed");

    expect_public_materialization_failure(
        "text-outside-t",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si>bad-text</si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found text outside a text element",
        "sharedStrings item text outside t");

    expect_public_materialization_failure(
        "nested-si",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><si><t>nested</t></si></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found a nested shared string item",
        "nested sharedStrings item");

    expect_public_materialization_failure(
        "nested-markup-in-t",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>bad<r/>text</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found nested markup inside a text element",
        "sharedStrings text element with nested markup");

    expect_public_materialization_failure(
        "comment-inside-text",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>bad<!--hidden-->text</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found nested markup inside a text element",
        "sharedStrings text element with comment markup");

    expect_public_materialization_failure(
        "processing-instruction-inside-text",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>bad<?fx hidden?>text</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found nested markup inside a text element",
        "sharedStrings text element with processing instruction markup");

    expect_public_materialization_failure(
        "malformed-processing-instruction-before-root",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?fastxlsx broken>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings payload with malformed processing instruction before root");

    expect_public_materialization_failure(
        "malformed-processing-instruction-inside-item",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><?fastxlsx broken><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings item with malformed processing instruction");

    expect_public_materialization_failure(
        "empty-processing-instruction-target-before-root",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<? ?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings payload with empty processing instruction target before root");

    expect_public_materialization_failure(
        "empty-processing-instruction-target-inside-item",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><? ?><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings item with empty processing instruction target");

    expect_public_materialization_failure(
        "invalid-processing-instruction-target-start-before-root",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?-bad?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings payload with invalid processing instruction target start before root");

    expect_public_materialization_failure(
        "invalid-processing-instruction-target-start-inside-item",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><?-bad?><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings item with invalid processing instruction target start");

    expect_public_materialization_failure(
        "processing-instruction-target-missing-separator-before-root",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?target?data?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings payload with PI target missing separator before root");

    expect_public_materialization_failure(
        "processing-instruction-target-missing-separator-inside-item",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><?target?data?><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings item with PI target missing separator");

    expect_public_materialization_failure(
        "invalid-processing-instruction-target-char-before-root",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?bad^name?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings payload with invalid processing instruction target character before root");

    expect_public_materialization_failure(
        "invalid-processing-instruction-target-char-inside-item",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><?bad^name?><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed processing instruction",
        "sharedStrings item with invalid processing instruction target character");

    expect_public_materialization_failure(
        "xml-declaration-inside-item",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><?xml version="1.0"?><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found XML declaration after sharedStrings root start",
        "sharedStrings item with nested XML declaration");

    expect_public_materialization_failure(
        "duplicate-xml-declaration",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<?xml version="1.0"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found duplicate XML declaration",
        "sharedStrings payload with duplicate XML declaration");

    expect_public_materialization_failure(
        "xml-like-processing-instruction-before-root",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?XML version="1.0"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found reserved XML processing instruction target",
        "sharedStrings payload with uppercase XML-like processing instruction before root");

    expect_public_materialization_failure(
        "xml-like-processing-instruction-inside-item",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><?Xml version="1.0"?><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found reserved XML processing instruction target",
        "sharedStrings item with mixed-case XML-like processing instruction");

    expect_public_materialization_failure(
        "xml-declaration-missing-version",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with XML declaration missing version");

    expect_public_materialization_failure(
        "xml-declaration-unsupported-version",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="2.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found unsupported XML declaration version",
        "sharedStrings payload with unsupported XML declaration version");

    expect_public_materialization_failure(
        "xml-declaration-duplicate-encoding",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8" encoding="UTF-16"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with duplicate XML declaration encoding");

    expect_public_materialization_failure(
        "xml-declaration-unknown-attribute",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" flavor="fastxlsx"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with unknown XML declaration attribute");

    expect_public_materialization_failure(
        "xml-declaration-encoding-after-standalone",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" standalone="yes" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with XML declaration encoding after standalone");

    expect_public_materialization_failure(
        "xml-declaration-duplicate-standalone",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" standalone="yes" standalone="no"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with duplicate XML declaration standalone");

    expect_public_materialization_failure(
        "xml-declaration-empty-standalone",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" standalone=""?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with empty XML declaration standalone");

    expect_public_materialization_failure(
        "xml-declaration-invalid-standalone",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" standalone="maybe"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with invalid XML declaration standalone value");

    expect_public_materialization_failure(
        "xml-declaration-empty-encoding",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding=""?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with empty XML declaration encoding");

    expect_public_materialization_failure(
        "xml-declaration-digit-start-encoding",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="8BIT"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with digit-start XML declaration encoding");

    expect_public_materialization_failure(
        "xml-declaration-invalid-encoding-character",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF:8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found malformed XML declaration",
        "sharedStrings payload with invalid XML declaration encoding character");

    expect_public_materialization_failure(
        "comment-before-xml-declaration",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<!--before-declaration-->)"
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found XML declaration after sharedStrings prolog markup",
        "sharedStrings payload with comment before XML declaration");

    expect_public_materialization_failure(
        "processing-instruction-before-xml-declaration",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?fastxlsx before-declaration?>)"
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found XML declaration after sharedStrings prolog markup",
        "sharedStrings payload with processing instruction before XML declaration");

    expect_public_materialization_failure(
        "whitespace-before-xml-declaration",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                " \r\n\t"
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found XML declaration after sharedStrings prolog text",
        "sharedStrings payload with whitespace before XML declaration");

    expect_public_materialization_failure(
        "xml-declaration-after-root",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>real</t></si>)"
                R"(</sst><?xml version="1.0"?>)";
        },
        "CellStore sharedStrings loader found XML declaration after sharedStrings root start",
        "sharedStrings payload with XML declaration after root");

    expect_public_materialization_failure(
        "cdata-inside-text",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t><![CDATA[hidden]]></t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found nested markup inside a text element",
        "sharedStrings text element with CDATA markup");

    expect_public_materialization_failure(
        "cdata-outside-text",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><![CDATA[hidden]]><t>real</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found unsupported markup declaration",
        "sharedStrings item with unsupported CDATA declaration outside text");

    expect_public_materialization_failure(
        "mismatched-closing-tags",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>bad</r></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found mismatched closing tags",
        "sharedStrings mismatched closing tag");

    expect_public_materialization_failure(
        "unknown-entity",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>A&bogus;</t></si>)"
                R"(</sst>)";
        },
        "CellStore worksheet loader found an unknown XML entity reference",
        "sharedStrings text with unknown XML entity");

    expect_public_materialization_failure(
        "unterminated-entity",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>A&amp</t></si>)"
                R"(</sst>)";
        },
        "CellStore worksheet loader found an unterminated XML entity",
        "sharedStrings text with unterminated XML entity");

    expect_public_materialization_failure(
        "character-reference-out-of-range",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si><t>A&#x110000;</t></si>)"
                R"(</sst>)";
        },
        "CellStore worksheet loader XML character reference exceeds Unicode range",
        "sharedStrings text with out-of-range XML character reference");

    expect_public_materialization_failure(
        "attribute-without-value",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si bad><t>bad</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found an attribute without a value",
        "sharedStrings tag with missing attribute value");

    expect_public_materialization_failure(
        "unquoted-attribute",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si bad=1><t>bad</t></si>)"
                R"(</sst>)";
        },
        "CellStore sharedStrings loader found an unquoted attribute value",
        "sharedStrings tag with unquoted attribute value");

    expect_public_materialization_failure(
        "unterminated-attribute",
        [](std::map<std::string, std::string>& entries) {
            entries.at("xl/sharedStrings.xml") =
                R"(<?xml version="1.0" encoding="UTF-8"?>)"
                R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
                R"(<si bad="unterminated><t>bad</t></si>)"
                R"(</sst>)";
        },
        "CellStore worksheet loader found a truncated XML tag",
        "sharedStrings tag with unterminated attribute value");
}

void test_public_worksheet_editor_materializes_source_style_ids_and_rejects_malformed_style_attributes()
{
    const auto expect_public_style_materialization_failure =
        [](std::string_view tag,
            const std::function<std::filesystem::path(std::string_view)>& write_source,
            std::string_view expected_diagnostic,
            std::string_view scenario) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-style-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-style-")
                + std::string(tag) + "-output.xlsx");

            const std::string replacement_text =
                std::string("usable-after-source-style-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(source, output,
                expected_diagnostic, replacement_text, scenario);
        };

    {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-source-style-non-default-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-source-style-non-default-output.xlsx");
        fastxlsx::StyleId number_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
            number_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("styled-source")
                    .with_style(number_style),
                fastxlsx::CellView::text("clear-source").with_style(number_style),
                fastxlsx::CellView::text("full-replace-source").with_style(number_style)});
            data.append_row({fastxlsx::CellView::text("range-clear-left")
                    .with_style(number_style),
                fastxlsx::CellView::text("range-clear-right").with_style(number_style)});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-source-style")});
            writer.close();
        }

        const auto source_entries = fastxlsx::test::read_zip_entries(source);
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
        check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
                && a1->text_value() == "styled-source" && a1->has_style()
                && a1->style_id().value() == number_style.value(),
            "WorksheetEditor should materialize non-default source style ids");
        check(!sheet.has_pending_changes(),
            "source style materialization should start as a clean read-only session");
        check(!editor.has_pending_changes(),
            "source style materialization should not dirty WorkbookEditor");

        const bool rejected_caller_style = threw_fastxlsx_error([&sheet, number_style]() {
            sheet.set_cell_value("A1",
                fastxlsx::CellValue::text("must-not-overwrite-style")
                    .with_style(number_style));
        });
        check(rejected_caller_style,
            "WorksheetEditor::set_cell_value should reject caller-supplied non-default style ids");
        const fastxlsx::CellValue preserved_after_reject = sheet.get_cell("A1");
        check(preserved_after_reject.kind() == fastxlsx::CellValueKind::Text
                && preserved_after_reject.text_value() == "styled-source"
                && preserved_after_reject.has_style()
                && preserved_after_reject.style_id().value() == number_style.value(),
            "rejected style-preserving value edit should not mutate the source cell");
        check(editor.last_edit_error().has_value()
                && editor.last_edit_error()->find("set_cell_value()") != std::string::npos,
            "rejected style-preserving value edit should update last_edit_error");

        const std::vector<fastxlsx::WorksheetCellUpdate> rejected_caller_style_batch = {
            {fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::CellValue::text("must-not-overwrite-batch-style")
                    .with_style(number_style)}};
        const bool rejected_batch_style = threw_fastxlsx_error(
            [&sheet, &rejected_caller_style_batch]() {
                sheet.set_cells(rejected_caller_style_batch);
            });
        check(rejected_batch_style,
            "WorksheetEditor::set_cells should reject caller-supplied non-default style ids");
        const fastxlsx::CellValue preserved_b1_after_batch_reject = sheet.get_cell("B1");
        check(preserved_b1_after_batch_reject.kind() == fastxlsx::CellValueKind::Text
                && preserved_b1_after_batch_reject.text_value() == "clear-source"
                && preserved_b1_after_batch_reject.has_style()
                && preserved_b1_after_batch_reject.style_id().value() == number_style.value(),
            "rejected batch style edit should not mutate the source cell");
        check(editor.last_edit_error().has_value()
                && editor.last_edit_error()->find("set_cells()") != std::string::npos,
            "rejected batch style edit should update last_edit_error");

        const std::vector<fastxlsx::WorksheetCellUpdate> rejected_caller_style_value_batch = {
            {fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::CellValue::text("must-not-overwrite-value-batch-style")
                    .with_style(number_style)}};
        const bool rejected_value_batch_style = threw_fastxlsx_error(
            [&sheet, &rejected_caller_style_value_batch]() {
                sheet.set_cell_values(rejected_caller_style_value_batch);
            });
        check(rejected_value_batch_style,
            "WorksheetEditor::set_cell_values should reject caller-supplied non-default style ids");
        const fastxlsx::CellValue preserved_b1_after_value_batch_reject = sheet.get_cell("B1");
        check(preserved_b1_after_value_batch_reject.kind() == fastxlsx::CellValueKind::Text
                && preserved_b1_after_value_batch_reject.text_value() == "clear-source"
                && preserved_b1_after_value_batch_reject.has_style()
                && preserved_b1_after_value_batch_reject.style_id().value() == number_style.value(),
            "rejected value batch style edit should not mutate the source cell");
        check(editor.last_edit_error().has_value()
                && editor.last_edit_error()->find("set_cell_values()") != std::string::npos,
            "rejected value batch style edit should update last_edit_error");

        sheet.clear_cell_value("E1");
        check(!sheet.try_cell("E1").has_value(),
            "WorksheetEditor::clear_cell_value should not synthesize a missing cell");
        check(!sheet.has_pending_changes(),
            "missing clear_cell_value should not dirty the materialized session");
        check(!editor.last_edit_error().has_value(),
            "missing clear_cell_value should clear prior edit diagnostics");

        sheet.clear_cell_values(fastxlsx::CellRange {2, 3, 2, 4});
        check(!sheet.try_cell(2, 3).has_value(),
            "WorksheetEditor::clear_cell_values should not synthesize missing range cells");
        check(!sheet.has_pending_changes(),
            "missing-only clear_cell_values should not dirty the materialized session");
        check(!editor.last_edit_error().has_value(),
            "missing-only clear_cell_values should keep public edit diagnostics clear");

        sheet.erase_cells(fastxlsx::CellRange {4, 4, 4, 5});
        check(!sheet.try_cell(4, 4).has_value(),
            "WorksheetEditor::erase_cells range should not synthesize missing cells");
        check(!sheet.has_pending_changes(),
            "missing-only erase_cells range should not dirty the materialized session");
        check(!editor.last_edit_error().has_value(),
            "missing-only erase_cells range should keep public edit diagnostics clear");

        const std::vector<fastxlsx::WorksheetCellUpdate> empty_batch;
        sheet.set_cells(empty_batch);
        check(!sheet.has_pending_changes(),
            "empty set_cells batch should not dirty the materialized session");
        check(!editor.last_edit_error().has_value(),
            "empty set_cells batch should keep public edit diagnostics clear");

        const std::vector<fastxlsx::WorksheetCellUpdate> empty_value_batch;
        sheet.set_cell_values(empty_value_batch);
        check(!sheet.has_pending_changes(),
            "empty set_cell_values batch should not dirty the materialized session");
        check(!editor.last_edit_error().has_value(),
            "empty set_cell_values batch should keep public edit diagnostics clear");

        const std::vector<fastxlsx::WorksheetCellReference> empty_clear_batch;
        sheet.clear_cell_values(empty_clear_batch);
        check(!sheet.has_pending_changes(),
            "empty coordinate clear_cell_values batch should not dirty the materialized session");
        check(!editor.last_edit_error().has_value(),
            "empty coordinate clear_cell_values batch should keep public edit diagnostics clear");

        const std::vector<fastxlsx::WorksheetCellReference> empty_erase_batch;
        sheet.erase_cells(empty_erase_batch);
        check(!sheet.has_pending_changes(),
            "empty coordinate erase_cells batch should not dirty the materialized session");
        check(!editor.last_edit_error().has_value(),
            "empty coordinate erase_cells batch should keep public edit diagnostics clear");

        const std::vector<fastxlsx::WorksheetCellUpdate> value_batch_updates = {
            {fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::CellValue::text("style-preserved-batch-value-edit")},
            {fastxlsx::WorksheetCellReference {1, 6},
                fastxlsx::CellValue::text("value-batch-first")},
            {fastxlsx::WorksheetCellReference {1, 6},
                fastxlsx::CellValue::text("value-batch-last")},
            {fastxlsx::WorksheetCellReference {1, 8},
                fastxlsx::CellValue::text("erase-batch-target")},
            {fastxlsx::WorksheetCellReference {1, 10},
                fastxlsx::CellValue::text("range-erase-left")},
            {fastxlsx::WorksheetCellReference {1, 11},
                fastxlsx::CellValue::text("range-erase-right")}};
        sheet.set_cell_values(value_batch_updates);
        const fastxlsx::CellValue batch_updated_a1 = sheet.get_cell("A1");
        check(batch_updated_a1.kind() == fastxlsx::CellValueKind::Text
                && batch_updated_a1.text_value() == "style-preserved-batch-value-edit"
                && batch_updated_a1.has_style()
                && batch_updated_a1.style_id().value() == number_style.value(),
            "WorksheetEditor::set_cell_values should preserve source style ids");
        const fastxlsx::CellValue duplicate_value_batch_f1 = sheet.get_cell("F1");
        check(duplicate_value_batch_f1.kind() == fastxlsx::CellValueKind::Text
                && duplicate_value_batch_f1.text_value() == "value-batch-last"
                && !duplicate_value_batch_f1.has_style(),
            "WorksheetEditor::set_cell_values should apply duplicate coordinates in input order");
        const fastxlsx::CellValue erase_batch_target_h1 = sheet.get_cell("H1");
        check(erase_batch_target_h1.kind() == fastxlsx::CellValueKind::Text
                && erase_batch_target_h1.text_value() == "erase-batch-target"
                && !erase_batch_target_h1.has_style(),
            "WorksheetEditor::set_cell_values should create a target for erase_cells");
        const fastxlsx::CellValue range_erase_target_j1 = sheet.get_cell("J1");
        check(range_erase_target_j1.kind() == fastxlsx::CellValueKind::Text
                && range_erase_target_j1.text_value() == "range-erase-left"
                && !range_erase_target_j1.has_style(),
            "WorksheetEditor::set_cell_values should create the left target for range erase");
        const fastxlsx::CellValue range_erase_target_k1 = sheet.get_cell("K1");
        check(range_erase_target_k1.kind() == fastxlsx::CellValueKind::Text
                && range_erase_target_k1.text_value() == "range-erase-right"
                && !range_erase_target_k1.has_style(),
            "WorksheetEditor::set_cell_values should create the right target for range erase");
        check(!editor.last_edit_error().has_value(),
            "successful set_cell_values batch should keep public edit diagnostics clear");

        const std::vector<fastxlsx::WorksheetCellReference> coordinate_clear_batch = {
            fastxlsx::WorksheetCellReference {1, 6},
            fastxlsx::WorksheetCellReference {1, 7}};
        sheet.clear_cell_values(coordinate_clear_batch);
        const fastxlsx::CellValue cleared_f1 = sheet.get_cell("F1");
        check(cleared_f1.kind() == fastxlsx::CellValueKind::Blank && !cleared_f1.has_style(),
            "coordinate clear_cell_values should clear represented unstyled cells");
        check(!sheet.try_cell("G1").has_value(),
            "coordinate clear_cell_values should not synthesize missing cells");
        check(!editor.last_edit_error().has_value(),
            "successful coordinate clear_cell_values batch should keep public edit diagnostics clear");

        const std::vector<fastxlsx::WorksheetCellReference> coordinate_erase_batch = {
            fastxlsx::WorksheetCellReference {1, 8},
            fastxlsx::WorksheetCellReference {1, 9},
            fastxlsx::WorksheetCellReference {1, 8}};
        sheet.erase_cells(coordinate_erase_batch);
        check(!sheet.try_cell("H1").has_value(),
            "coordinate erase_cells should remove represented cells");
        check(!sheet.try_cell("I1").has_value(),
            "coordinate erase_cells should not synthesize missing cells");
        check(!editor.last_edit_error().has_value(),
            "successful coordinate erase_cells batch should keep public edit diagnostics clear");

        sheet.erase_cells(fastxlsx::CellRange {1, 10, 1, 12});
        check(!sheet.try_cell("J1").has_value(),
            "range erase_cells should remove the left represented cell");
        check(!sheet.try_cell("K1").has_value(),
            "range erase_cells should remove the right represented cell");
        check(!sheet.try_cell("L1").has_value(),
            "range erase_cells should not synthesize missing cells");
        check(!editor.last_edit_error().has_value(),
            "successful range erase_cells should keep public edit diagnostics clear");

        sheet.set_cell_value("A1",
            fastxlsx::CellValue::text("style-preserved-value-edit"));
        const fastxlsx::CellValue updated_a1 = sheet.get_cell("A1");
        check(updated_a1.kind() == fastxlsx::CellValueKind::Text
                && updated_a1.text_value() == "style-preserved-value-edit"
                && updated_a1.has_style()
                && updated_a1.style_id().value() == number_style.value(),
            "WorksheetEditor::set_cell_value should preserve the existing source style id");
        check(!editor.last_edit_error().has_value(),
            "successful style-preserving value edit should clear prior edit diagnostics");

        sheet.clear_cell_value(1, 2);
        const fastxlsx::CellValue cleared_b1 = sheet.get_cell("B1");
        check(cleared_b1.kind() == fastxlsx::CellValueKind::Blank
                && cleared_b1.has_style()
                && cleared_b1.style_id().value() == number_style.value(),
            "WorksheetEditor::clear_cell_value should preserve the existing source style id");

        sheet.clear_cell_values(fastxlsx::CellRange {2, 1, 2, 3});
        const fastxlsx::CellValue cleared_a2 = sheet.get_cell("A2");
        check(cleared_a2.kind() == fastxlsx::CellValueKind::Blank
                && cleared_a2.has_style()
                && cleared_a2.style_id().value() == number_style.value(),
            "WorksheetEditor::clear_cell_values should preserve the left source style id");
        const fastxlsx::CellValue cleared_b2 = sheet.get_cell("B2");
        check(cleared_b2.kind() == fastxlsx::CellValueKind::Blank
                && cleared_b2.has_style()
                && cleared_b2.style_id().value() == number_style.value(),
            "WorksheetEditor::clear_cell_values should preserve the right source style id");
        check(!sheet.try_cell("C2").has_value(),
            "WorksheetEditor::clear_cell_values should not synthesize missing cells");

        const std::vector<fastxlsx::WorksheetCellUpdate> batch_updates = {
            {fastxlsx::WorksheetCellReference {1, 3},
                fastxlsx::CellValue::text("batch-full-replace")},
            {fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::CellValue::text("batch-first")},
            {fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::CellValue::text("batch-last")},
            {fastxlsx::WorksheetCellReference {3, 2}, fastxlsx::CellValue::blank()}};
        sheet.set_cells(batch_updates);
        const fastxlsx::CellValue replaced_c1 = sheet.get_cell("C1");
        check(replaced_c1.kind() == fastxlsx::CellValueKind::Text
                && replaced_c1.text_value() == "batch-full-replace"
                && !replaced_c1.has_style(),
            "WorksheetEditor::set_cells should drop source style ids on full replacement");
        const fastxlsx::CellValue duplicate_target_a3 = sheet.get_cell("A3");
        check(duplicate_target_a3.kind() == fastxlsx::CellValueKind::Text
                && duplicate_target_a3.text_value() == "batch-last",
            "WorksheetEditor::set_cells should apply duplicate coordinates in input order");
        const fastxlsx::CellValue blank_b3 = sheet.get_cell("B3");
        check(blank_b3.kind() == fastxlsx::CellValueKind::Blank && !blank_b3.has_style(),
            "WorksheetEditor::set_cells should allow explicit unstyled blank replacements");
        check(!editor.last_edit_error().has_value(),
            "successful set_cells batch should keep public edit diagnostics clear");

        sheet.set_cell_value(1, 4, fastxlsx::CellValue::text("value-edit-no-source-style"));
        editor.save_as(output);

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
            "dirty source-style projection should preserve styles.xml bytes");
        const std::string output_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(output_xml,
            R"(<c r="A1" s="1" t="inlineStr"><is><t>style-preserved-value-edit</t></is></c>)",
            "dirty source-style projection should preserve style ids on value-only edits");
        check_contains(output_xml, R"(<c r="B1" s="1"/>)",
            "dirty source-style projection should write styled explicit blanks for clear_cell_value");
        check_contains(output_xml, R"(<c r="A2" s="1"/>)",
            "dirty source-style projection should write styled explicit blanks for range clear");
        check_contains(output_xml, R"(<c r="B2" s="1"/>)",
            "dirty source-style projection should write every represented range cell as blank");
        check_contains(output_xml,
            R"(<c r="C1" t="inlineStr"><is><t>batch-full-replace</t></is></c>)",
            "dirty source-style projection should drop styles on batch full cell replacement");
        check_contains(output_xml,
            R"(<c r="D1" t="inlineStr"><is><t>value-edit-no-source-style</t></is></c>)",
            "style-preserving value edit on a missing cell should not synthesize styles");
        check_contains(output_xml, R"(<c r="F1"/>)",
            "coordinate clear batch should persist unstyled explicit blanks");
        check_contains(output_xml,
            R"(<c r="A3" t="inlineStr"><is><t>batch-last</t></is></c>)",
            "dirty source-style projection should persist the later duplicate batch update");
        check_contains(output_xml, R"(<c r="B3"/>)",
            "dirty source-style projection should persist unstyled explicit batch blanks");
        check_not_contains(output_xml, R"(r="E1")",
            "missing clear_cell_value should not synthesize an output cell");
        check_not_contains(output_xml, R"(r="G1")",
            "coordinate clear batch should not synthesize missing output cells");
        check_not_contains(output_xml, R"(r="H1")",
            "coordinate erase batch should omit erased output cells");
        check_not_contains(output_xml, R"(r="I1")",
            "coordinate erase batch should not synthesize missing output cells");
        check_not_contains(output_xml, R"(r="J1")",
            "range erase should omit the left erased output cell");
        check_not_contains(output_xml, R"(r="K1")",
            "range erase should omit the right erased output cell");
        check_not_contains(output_xml, R"(r="L1")",
            "range erase should not synthesize missing output cells");
        check_not_contains(output_xml, R"(r="C2")",
            "range clear should not synthesize missing output cells");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-source-style",
            "dirty source-style projection should preserve untouched sheets");
    }

    constexpr std::string_view styles_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles";
    const auto remove_workbook_relationship_by_type =
        [](std::string& relationships_xml, std::string_view relationship_type) {
            const std::string type_attribute =
                std::string(R"(Type=")") + std::string(relationship_type) + R"(")";
            const std::size_t type_position = relationships_xml.find(type_attribute);
            if (type_position == std::string::npos) {
                throw std::runtime_error("test workbook relationship type was not found");
            }
            const std::size_t relationship_begin =
                relationships_xml.rfind("<Relationship", type_position);
            const std::size_t relationship_end = relationships_xml.find("/>", type_position);
            if (relationship_begin == std::string::npos
                || relationship_end == std::string::npos) {
                throw std::runtime_error("test workbook relationship element was not found");
            }
            relationships_xml.erase(
                relationship_begin, relationship_end + 2 - relationship_begin);
        };

    const auto write_source_with_valid_style_id = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
            const fastxlsx::StyleId number_style =
                writer.add_style(fastxlsx::CellStyle {"0.00"});
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("styled-source")
                    .with_style(number_style)});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-source-style")});
            writer.close();
        }
        return source;
    };

    expect_public_style_materialization_failure(
        "missing-styles-relationship",
        [&](std::string_view name) {
            const std::filesystem::path source = write_source_with_valid_style_id(name);
            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            remove_workbook_relationship_by_type(
                entries.at("xl/_rels/workbook.xml.rels"), styles_relationship_type);
            write_stored_zip_entries(source, entries);
            return source;
        },
        "source style ids without a styles part",
        "non-default source style id without a workbook styles relationship");

    expect_public_style_materialization_failure(
        "wrong-styles-content-type",
        [&](std::string_view name) {
            const std::filesystem::path source = write_source_with_valid_style_id(name);
            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            replace_first_or_throw(entries.at("[Content_Types].xml"),
                "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml",
                "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
            write_stored_zip_entries(source, entries);
            return source;
        },
        "workbook styles relationship target is not a styles part",
        "non-default source style id with wrong styles content type");

    expect_public_style_materialization_failure(
        "out-of-range-style-id",
        [&](std::string_view name) {
            const std::filesystem::path source = write_source_with_valid_style_id(name);
            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/styles.xml") =
                R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
                R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/></cellXfs>)"
                R"(</styleSheet>)";
            write_stored_zip_entries(source, entries);
            return source;
        },
        "source style id out of range",
        "non-default source style id outside source styles cellXfs");

    const auto write_source_with_style_attribute = [](std::string_view style_attribute) {
        return [style_attribute = std::string(style_attribute)](std::string_view name) {
            const std::filesystem::path source = artifact(name);
            {
                fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
                fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
                data.append_row({fastxlsx::CellView::text("loadable-before-style-token"),
                    fastxlsx::CellView::text("invalid-default-like-style-token")});
                fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
                untouched.append_row({fastxlsx::CellView::text("keep-invalid-style-token")});
                writer.close();
            }

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="B1" t="inlineStr">)",
                std::string(R"(<c r="B1" )") + style_attribute + R"( t="inlineStr">)");
            write_stored_zip_entries(source, entries);
            return source;
        };
    };

    const auto write_source_with_qualified_style_attribute =
        [](std::string_view style_attribute) {
            return [style_attribute = std::string(style_attribute)](std::string_view name) {
                const std::filesystem::path source = artifact(name);
                {
                    fastxlsx::WorkbookWriter writer =
                        fastxlsx::WorkbookWriter::create(source);
                    fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
                    data.append_row({fastxlsx::CellView::text("loadable-before-qualified-style"),
                        fastxlsx::CellView::text("invalid-qualified-style-token")});
                    fastxlsx::WorksheetWriter untouched =
                        writer.add_worksheet("Untouched");
                    untouched.append_row(
                        {fastxlsx::CellView::text("keep-qualified-style-token")});
                    writer.close();
                }

                std::map<std::string, std::string> entries =
                    fastxlsx::test::read_zip_entries(source);
                std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
                replace_first_or_throw(worksheet_xml,
                    R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)",
                    R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:x="urn:fastxlsx-test-style">)");
                replace_first_or_throw(worksheet_xml,
                    R"(<c r="B1" t="inlineStr">)",
                    std::string(R"(<c r="B1" )") + style_attribute
                        + R"( t="inlineStr">)");
                write_stored_zip_entries(source, entries);
                return source;
            };
        };

    expect_public_style_materialization_failure(
        "empty-default-like",
        write_source_with_style_attribute(R"(s="")"),
        "invalid style id reference",
        "empty default-like source style id");
    expect_public_style_materialization_failure(
        "leading-zero-default-like",
        write_source_with_style_attribute(R"(s="00")"),
        "invalid style id reference",
        "leading-zero default-like source style id");
    expect_public_style_materialization_failure(
        "signed-zero-default-like",
        write_source_with_style_attribute(R"(s="+0")"),
        "invalid style id reference",
        "signed-zero default-like source style id");
    expect_public_style_materialization_failure(
        "whitespace-default-like",
        write_source_with_style_attribute(R"(s=" 0 ")"),
        "invalid style id reference",
        "whitespace-padded default-like source style id");
    expect_public_style_materialization_failure(
        "entity-default-like",
        write_source_with_style_attribute(R"(s="&#48;")"),
        "invalid style id reference",
        "entity-encoded default-like source style id");
    expect_public_style_materialization_failure(
        "valueless-default-like",
        write_source_with_style_attribute(R"(s)"),
        "found an attribute without a value",
        "valueless default-like source style id");
    expect_public_style_materialization_failure(
        "unquoted-default-like",
        write_source_with_style_attribute(R"(s=0)"),
        "found an unquoted attribute value",
        "unquoted default-like source style id");
    expect_public_style_materialization_failure(
        "duplicate-default-like",
        write_source_with_style_attribute(R"(s="0" s="0")"),
        "found duplicate attributes",
        "duplicate default-like source style id");
    expect_public_style_materialization_failure(
        "qualified-default-like",
        write_source_with_qualified_style_attribute(R"(x:s="0")"),
        "does not load cell metadata attributes",
        "qualified default-like source style id");
}

void test_public_worksheet_editor_rejects_unsupported_source_cell_shapes_cleanly()
{
    const auto write_text_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("source-shape")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-source-shape")});
        writer.close();
        return source;
    };

    const auto write_boolean_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::boolean(true)});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-boolean-shape")});
        writer.close();
        return source;
    };

    const auto expect_public_shape_materialization_failure =
        [](std::string_view tag,
            const std::function<std::filesystem::path(std::string_view)>& write_source,
            const std::function<void(std::map<std::string, std::string>&)>& mutate_entries,
            std::string_view expected_diagnostic,
            std::string_view scenario,
            bool check_clean_noop_recovery = false) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-shape-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-shape-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            mutate_entries(entries);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-shape-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(
                source, output, expected_diagnostic, replacement_text, scenario,
                "Data", "xl/worksheets/sheet1.xml", "Data", check_clean_noop_recovery);
        };

    expect_public_shape_materialization_failure(
        "date-cell",
        write_text_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-shape</t></is></c>)",
                R"(<c r="A1" t="d"><v>2026-06-17T00:00:00Z</v></c>)");
        },
        "unsupported cell type: d",
        "source date-like cell");

    expect_public_shape_materialization_failure(
        "custom-cell-type",
        write_text_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-shape</t></is></c>)",
                R"(<c r="A1" t="z"><v>custom-token</v></c>)");
        },
        "unsupported cell type: z",
        "source custom cell type");

    expect_public_shape_materialization_failure(
        "invalid-boolean",
        write_boolean_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="b"><v>1</v></c>)",
                R"(<c r="A1" t="b"><v>2</v></c>)");
        },
        "invalid boolean cell value",
        "source invalid boolean cell");

    expect_public_shape_materialization_failure(
        "missing-error-value",
        write_text_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-shape</t></is></c>)",
                R"(<c r="A1" t="e"></c>)");
        },
        "invalid error cell value",
        "source missing error cell value",
        true);

    expect_public_shape_materialization_failure(
        "empty-error-value",
        write_text_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-shape</t></is></c>)",
                R"(<c r="A1" t="e"><v></v></c>)");
        },
        "invalid error cell value",
        "source empty error cell value",
        true);
}

void test_public_worksheet_editor_rejects_malformed_source_worksheet_xml_cleanly()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-malformed-source-worksheet-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-malformed-source-worksheet-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("malformed-source")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-malformed-source")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    replace_first_or_throw(worksheet_xml, "</worksheet>", "");
    write_stored_zip_entries(source, entries);

    check_public_worksheet_materialization_failure_hygiene(source, output,
        "worksheet event reader requires a closing worksheet root",
        "usable-after-malformed-source-worksheet",
        "malformed source worksheet XML",
        "Untouched",
        "xl/worksheets/sheet2.xml",
        "Data",
        true);
}

void test_public_worksheet_editor_rejects_source_cell_reference_issues_cleanly()
{
    const auto write_text_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("source-reference")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-source-reference")});
        writer.close();
        return source;
    };

    const auto expect_public_reference_materialization_failure =
        [&](std::string_view tag,
            const std::function<void(std::map<std::string, std::string>&)>& mutate_entries,
            std::string_view expected_diagnostic,
            std::string_view scenario) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-reference-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_text_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-reference-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            mutate_entries(entries);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-reference-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(
                source, output, expected_diagnostic, replacement_text, scenario);
        };

    expect_public_reference_materialization_failure(
        "missing-r",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-reference</t></is></c>)",
                R"(<c t="inlineStr"><is><t>source-reference</t></is></c>)");
        },
        "CellStore worksheet loader requires explicit cell references",
        "missing source cell reference");

    expect_public_reference_materialization_failure(
        "row-cell-mismatch",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr">)",
                R"(<c r="A2" t="inlineStr">)");
        },
        "CellStore worksheet loader row and cell reference do not match",
        "source row/cell reference mismatch");
}

void test_public_worksheet_editor_rejects_source_formula_shapes_cleanly()
{
    const auto write_formula_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("A1+1")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-formula-shape")});
        writer.close();
        return source;
    };

    const auto write_text_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("formula-shape")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-formula-shape")});
        writer.close();
        return source;
    };

    const auto write_shared_string_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("formula-shape-shared")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-formula-shape")});
        writer.close();
        return source;
    };

    const auto expect_public_formula_materialization_failure =
        [](std::string_view tag,
            const std::function<std::filesystem::path(std::string_view)>& write_source,
            const std::function<void(std::map<std::string, std::string>&)>& mutate_entries,
            std::string_view expected_diagnostic,
            std::string_view scenario,
            bool check_clean_noop_recovery = false) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-formula-shape-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-formula-shape-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            mutate_entries(entries);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-formula-shape-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(
                source,
                output,
                expected_diagnostic,
                replacement_text,
                scenario,
                "Data",
                "xl/worksheets/sheet1.xml",
                "Data",
                check_clean_noop_recovery);
        };

    expect_public_formula_materialization_failure(
        "empty-formula",
        write_formula_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1"><f>A1+1</f></c>)",
                R"(<c r="A1"><f/></c>)");
        },
        "CellStore worksheet loader found an empty formula text",
        "empty source formula",
        true);

    expect_public_formula_materialization_failure(
        "duplicate-formula",
        write_formula_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1"><f>A1+1</f></c>)",
                R"(<c r="A1"><f>A1+1</f><f>A1+2</f></c>)");
        },
        "CellStore worksheet loader found duplicate formula elements",
        "duplicate source formula elements");

    expect_public_formula_materialization_failure(
        "non-numeric-formula",
        write_text_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>formula-shape</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><f>A1+1</f></c>)");
        },
        "CellStore worksheet loader found a formula in an unsupported cell type",
        "formula in unsupported source cell");

    expect_public_formula_materialization_failure(
        "shared-string-formula",
        write_shared_string_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="s"><v>0</v></c>)",
                R"(<c r="A1" t="s"><f>A1+1</f><v>0</v></c>)");
        },
        "CellStore worksheet loader found a formula in an unsupported cell type",
        "formula in shared-string source cell");

    expect_public_formula_materialization_failure(
        "missing-materializable-value",
        write_formula_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1"><f>A1+1</f></c>)",
                R"(<c r="A1"><f t="shared" si="0"/></c>)");
        },
        "CellStore worksheet loader found a formula without a materializable value",
        "formula without a materializable value");

    expect_public_formula_materialization_failure(
        "unsupported-formula-attribute",
        write_formula_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1"><f>A1+1</f></c>)",
                R"(<c r="A1"><f cm="1">A1+1</f></c>)");
        },
        "CellStore worksheet loader does not load unsupported formula attributes",
        "unsupported source formula attribute");

    expect_public_formula_materialization_failure(
        "invalid-shared-formula-index",
        write_formula_source,
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1"><f>A1+1</f></c>)",
                R"(<c r="A1"><f t="shared" si="abc">A1+1</f></c>)");
        },
        "CellStore worksheet loader found an invalid shared formula index",
        "invalid shared formula index");
}

void test_public_worksheet_editor_rejects_source_inline_text_shapes_cleanly()
{
    const auto write_text_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("source-inline")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-inline-shape")});
        writer.close();
        return source;
    };

    const auto expect_public_inline_materialization_failure =
        [&](std::string_view tag,
            const std::function<void(std::map<std::string, std::string>&)>& mutate_entries,
            std::string_view expected_diagnostic,
            std::string_view scenario,
            bool check_clean_noop_recovery = false) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-inline-shape-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_text_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-inline-shape-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            mutate_entries(entries);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-inline-shape-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(
                source,
                output,
                expected_diagnostic,
                replacement_text,
                scenario,
                "Data",
                "xl/worksheets/sheet1.xml",
                "Data",
                check_clean_noop_recovery);
        };

    expect_public_inline_materialization_failure(
        "unknown-entity",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><t>bad &unknown;</t></is></c>)");
        },
        "CellStore worksheet loader found an unknown XML entity reference",
        "source inline unknown XML entity",
        true);

    expect_public_inline_materialization_failure(
        "text-attributes",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><t foo="1">source-inline</t></is></c>)");
        },
        "CellStore worksheet loader does not load inline text attributes",
        "source inline text unsupported attributes");

    expect_public_inline_materialization_failure(
        "duplicate-text",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><t>source-</t><t>inline</t></is></c>)");
        },
        "CellStore worksheet loader found duplicate inline text elements",
        "source duplicate inline text elements");

    expect_public_inline_materialization_failure(
        "direct-plus-rich-text",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><t>source-</t><r><t>inline</t></r></is></c>)");
        },
        "CellStore worksheet loader found duplicate inline text elements",
        "source mixed direct and rich inline text");

    expect_public_inline_materialization_failure(
        "rpr-outside-run",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><rPr><b/></rPr></is></c>)");
        },
        "CellStore worksheet loader found malformed inline rich text metadata",
        "source inline rich properties outside run");

    expect_public_inline_materialization_failure(
        "text-inside-rpr",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><r><rPr><t>not-text</t></rPr><t>source-inline</t></r></is></c>)");
        },
        "CellStore worksheet loader found malformed inline rich text metadata",
        "source value markup inside inline rich properties");

    expect_public_inline_materialization_failure(
        "ignored-metadata-nested-si",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><rPh sb="0" eb="1"><si><t>decoy</t></si></rPh><t>source-inline</t></is></c>)");
        },
        "CellStore worksheet loader found malformed inline rich text metadata",
        "source inline ignored metadata with nested si decoy");

    expect_public_inline_materialization_failure(
        "ignored-metadata-nested-markup-in-text",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><rPh sb="0" eb="1"><t>ignored<r/>text</t></rPh><t>source-inline</t></is></c>)");
        },
        "CellStore worksheet loader found malformed inline rich text metadata",
        "source inline ignored metadata with nested markup inside text");

    expect_public_inline_materialization_failure(
        "ignored-metadata-orphan-closing",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></rPh></is></c>)");
        },
        "CellStore worksheet loader found malformed inline rich text metadata",
        "source inline ignored metadata orphan closing tag");

    expect_public_inline_materialization_failure(
        "ignored-metadata-unclosed",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><rPh sb="0" eb="1"><t>ignored</t></is></c>)");
        },
        "CellStore worksheet loader found malformed inline rich text metadata",
        "source inline ignored metadata left unclosed");

    expect_public_inline_materialization_failure(
        "unknown-metadata",
        [](std::map<std::string, std::string>& entries) {
            std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
            replace_first_or_throw(worksheet_xml,
                R"(<c r="A1" t="inlineStr"><is><t>source-inline</t></is></c>)",
                R"(<c r="A1" t="inlineStr"><is><unknownInlineMetadata/></is></c>)");
        },
        "CellStore worksheet loader does not load unsupported inline string metadata",
        "source unknown inline string metadata");
}

void test_public_worksheet_editor_rejects_source_row_cell_structure_cleanly()
{
    const auto write_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0)});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-row-cell-structure")});
        writer.close();
        return source;
    };

    const auto worksheet_xml = [](std::string_view sheet_data) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + std::string(sheet_data) + "</worksheet>";
    };

    const auto expect_public_structure_materialization_failure =
        [&](std::string_view tag,
            std::string_view replacement_worksheet_xml,
            std::string_view expected_diagnostic,
            std::string_view scenario) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-row-cell-structure-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-row-cell-structure-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/worksheets/sheet1.xml") =
                std::string(replacement_worksheet_xml);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-row-cell-structure-")
                + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(
                source, output, expected_diagnostic, replacement_text, scenario);
        };

    {
        const std::filesystem::path source = write_source(
            "fastxlsx-workbook-editor-public-source-row-cell-structure-row-metadata-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-source-row-cell-structure-row-metadata-output.xlsx");
        std::map<std::string, std::string> entries =
            fastxlsx::test::read_zip_entries(source);
        entries.at("xl/worksheets/sheet1.xml") = worksheet_xml(
            R"(<sheetData><row r="1" spans="1:13" s="4" customFormat="1" ht="20" customHeight="1"><c r="A1"><v>1</v></c></row></sheetData>)");
        write_stored_zip_entries(source, entries);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
        check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
                && a1->number_value() == 1.0,
            "WorksheetEditor should tolerate common source row metadata attributes");
        sheet.set_cell("B1", fastxlsx::CellValue::text("row-metadata-edited"));
        editor.save_as(output);

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string output_worksheet_xml =
            output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(output_worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
            "dirty row-metadata projection should keep materialized source cells");
        check_contains(output_worksheet_xml,
            R"(<c r="B1" t="inlineStr"><is><t>row-metadata-edited</t></is></c>)",
            "dirty row-metadata projection should write the new edit");
        check_not_contains(output_worksheet_xml, "customFormat",
            "dirty materialized projection should not claim to preserve source row metadata");
        check_not_contains(output_worksheet_xml, "spans=",
            "dirty materialized projection should drop source row span metadata");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"),
            "keep-row-cell-structure",
            "dirty row-metadata projection should preserve untouched sheets");
    }

    expect_public_structure_materialization_failure(
        "cell-metadata",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" cm="1"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader does not load cell metadata attributes",
        "source cell metadata attributes");

    expect_public_structure_materialization_failure(
        "duplicate-row",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row><row r="1"><c r="B1"><v>2</v></c></row></sheetData>)"),
        "CellStore worksheet loader found duplicate row numbers",
        "source duplicate row numbers");

    expect_public_structure_materialization_failure(
        "out-of-order-row",
        worksheet_xml(
            R"(<sheetData><row r="2"><c r="A2"><v>2</v></c></row><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader found out-of-order row numbers",
        "source out-of-order row numbers");

    expect_public_structure_materialization_failure(
        "out-of-order-cell",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="B1"><v>2</v></c><c r="A1"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader found out-of-order cell references",
        "source out-of-order cell references");

    expect_public_structure_materialization_failure(
        "invalid-numeric",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1"><v>1e999</v></c></row></sheetData>)"),
        "CellStore worksheet loader found an invalid numeric cell value",
        "source invalid numeric cell value");
}

void test_public_worksheet_editor_rejects_source_value_wrapper_shapes_cleanly()
{
    const auto write_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0)});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-value-wrapper")});
        writer.close();
        return source;
    };

    const auto worksheet_xml = [](std::string_view sheet_data) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + std::string(sheet_data) + "</worksheet>";
    };

    const auto expect_public_value_wrapper_materialization_failure =
        [&](std::string_view tag,
            std::string_view replacement_worksheet_xml,
            std::string_view expected_diagnostic,
            std::string_view scenario,
            std::string_view recovery_sheet_name = "Data",
            std::string_view output_entry_name = "xl/worksheets/sheet1.xml",
            bool check_clean_noop_recovery = false) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-value-wrapper-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-value-wrapper-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/worksheets/sheet1.xml") =
                std::string(replacement_worksheet_xml);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-value-wrapper-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(
                source,
                output,
                expected_diagnostic,
                replacement_text,
                scenario,
                recovery_sheet_name,
                output_entry_name,
                "Data",
                check_clean_noop_recovery);
        };

    expect_public_value_wrapper_materialization_failure(
        "scalar-attributes",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1"><v foo="1">1</v></c></row></sheetData>)"),
        "CellStore worksheet loader does not load scalar value attributes",
        "source scalar value attributes",
        "Data",
        "xl/worksheets/sheet1.xml",
        true);

    expect_public_value_wrapper_materialization_failure(
        "duplicate-scalar",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1"><v>1</v><v>2</v></c></row></sheetData>)"),
        "CellStore worksheet loader found duplicate scalar value elements",
        "source duplicate scalar value elements");

    expect_public_value_wrapper_materialization_failure(
        "inline-metadata-in-number",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1"><is><t>bad</t></is></c></row></sheetData>)"),
        "CellStore worksheet loader found inline-string metadata in a non-inline string cell",
        "source inline metadata in non-inline cell");

    expect_public_value_wrapper_materialization_failure(
        "scalar-in-inline",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><v>bad</v></c></row></sheetData>)"),
        "CellStore worksheet loader found a non-inline value in an inline string cell",
        "source scalar value in inline string cell");

    expect_public_value_wrapper_materialization_failure(
        "direct-cell-text",
        worksheet_xml(R"(<sheetData><row r="1"><c r="A1">direct-text</c></row></sheetData>)"),
        "CellStore worksheet loader found value text without a value tag",
        "source direct cell text without value wrapper");

    expect_public_value_wrapper_materialization_failure(
        "comment-inside-cell",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a<!--hidden-->b</t></is></c></row></sheetData>)"),
        "CellStore worksheet loader does not load cell comments, processing instructions, or unsupported markup",
        "source comments inside cell text");

    expect_public_value_wrapper_materialization_failure(
        "processing-instruction-inside-cell",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a<?fastxlsx hidden?>b</t></is></c></row></sheetData>)"),
        "CellStore worksheet loader does not load cell comments, processing instructions, or unsupported markup",
        "source processing instruction inside cell text");

    expect_public_value_wrapper_materialization_failure(
        "cdata-inside-cell",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a<![CDATA[hidden]]>b</t></is></c></row></sheetData>)"),
        "CellStore worksheet loader does not load cell comments, processing instructions, or unsupported markup",
        "source CDATA inside cell text");

    expect_public_value_wrapper_materialization_failure(
        "doctype-inside-cell",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a<!DOCTYPE fastxlsx>b</t></is></c></row></sheetData>)"),
        "CellStore worksheet loader does not load cell comments, processing instructions, or unsupported markup",
        "source DOCTYPE-like markup inside cell text");

    expect_public_value_wrapper_materialization_failure(
        "xml-declaration-inside-cell",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a<?xml version="1.0"?>b</t></is></c></row></sheetData>)"),
        "worksheet event reader found XML declaration after worksheet root",
        "source XML declaration inside cell text",
        "Untouched",
        "xl/worksheets/sheet2.xml");
}

void test_public_worksheet_editor_rejects_wrong_namespace_unsupported_local_names_cleanly()
{
    const auto write_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0)});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-wrong-namespace-local-name")});
        writer.close();
        return source;
    };

    const auto worksheet_xml = [](std::string_view sheet_data) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<bad:worksheet xmlns:bad="urn:fastxlsx:not-spreadsheetml">)"
            + std::string(sheet_data) + "</bad:worksheet>";
    };

    const auto expect_public_wrong_namespace_unsupported_local_name_failure =
        [&](std::string_view tag,
            std::string_view replacement_worksheet_xml,
            std::string_view expected_diagnostic,
            std::string_view scenario) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-wrong-namespace-unsupported-local-name-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-wrong-namespace-unsupported-local-name-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/worksheets/sheet1.xml") =
                std::string(replacement_worksheet_xml);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-wrong-namespace-unsupported-local-name-")
                + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(
                source, output, expected_diagnostic, replacement_text, scenario);
        };

    expect_public_wrong_namespace_unsupported_local_name_failure(
        "cell-metadata",
        worksheet_xml(
            R"(<bad:sheetData><bad:row r="1"><bad:c r="A1"><bad:unsupportedCellMetadata/></bad:c></bad:row></bad:sheetData>)"),
        "CellStore worksheet loader does not load unsupported cell metadata",
        "wrong-namespace unsupported cell metadata local-name");

    expect_public_wrong_namespace_unsupported_local_name_failure(
        "inline-metadata",
        worksheet_xml(
            R"(<bad:sheetData><bad:row r="1"><bad:c r="A1" t="inlineStr"><bad:is><bad:unsupportedInlineMetadata/></bad:is></bad:c></bad:row></bad:sheetData>)"),
        "CellStore worksheet loader does not load unsupported inline string metadata",
        "wrong-namespace unsupported inline string metadata local-name");
}

void test_public_worksheet_editor_rejects_source_xml_parser_issues_cleanly()
{
    const auto write_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("source-parser")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-source-parser")});
        writer.close();
        return source;
    };

    const auto worksheet_xml = [](std::string_view sheet_data) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + std::string(sheet_data) + "</worksheet>";
    };

    const auto expect_public_xml_parser_materialization_failure =
        [&](std::string_view tag,
            std::string_view replacement_worksheet_xml,
            std::string_view expected_diagnostic,
            std::string_view scenario) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-xml-parser-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-xml-parser-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/worksheets/sheet1.xml") =
                std::string(replacement_worksheet_xml);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-xml-parser-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(source,
                output,
                expected_diagnostic,
                replacement_text,
                scenario,
                "Untouched",
                "xl/worksheets/sheet2.xml");
        };

    expect_public_xml_parser_materialization_failure(
        "unterminated-entity",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &amp</t></is></c></row></sheetData>)"),
        "CellStore worksheet loader found an unterminated XML entity",
        "source unterminated XML entity");

    expect_public_xml_parser_materialization_failure(
        "invalid-character-reference",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &#xZZ;</t></is></c></row></sheetData>)"),
        "CellStore worksheet loader found an invalid XML character reference",
        "source invalid XML character reference");

    expect_public_xml_parser_materialization_failure(
        "out-of-range-character-reference",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &#x110000;</t></is></c></row></sheetData>)"),
        "CellStore worksheet loader XML character reference exceeds Unicode range",
        "source out-of-range XML character reference");

    expect_public_xml_parser_materialization_failure(
        "unquoted-attribute",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r=A1><v>1</v></c></row></sheetData>)"),
        "worksheet event reader found an unquoted attribute value",
        "source unquoted cell attribute");

    expect_public_xml_parser_materialization_failure(
        "duplicate-cell-reference-attribute",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" r="B1"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader found duplicate attributes",
        "source duplicate cell reference attributes");

    expect_public_xml_parser_materialization_failure(
        "duplicate-cell-type-attribute",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1" t="n" t="b"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader found duplicate attributes",
        "source duplicate cell type attributes");
}

void test_public_worksheet_editor_rejects_source_reference_boundaries_cleanly()
{
    const auto write_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0)});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-source-reference-boundary")});
        writer.close();
        return source;
    };

    const auto worksheet_xml = [](std::string_view sheet_data) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + std::string(sheet_data) + "</worksheet>";
    };

    const auto expect_public_reference_boundary_materialization_failure =
        [&](std::string_view tag,
            std::string_view replacement_worksheet_xml,
            std::string_view expected_diagnostic,
            std::string_view scenario) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-reference-boundary-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-reference-boundary-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/worksheets/sheet1.xml") =
                std::string(replacement_worksheet_xml);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-reference-boundary-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(source,
                output,
                expected_diagnostic,
                replacement_text,
                scenario,
                "Untouched",
                "xl/worksheets/sheet2.xml");
        };

    expect_public_reference_boundary_materialization_failure(
        "column-overflow",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="XFE1"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader cell column exceeds Excel limits",
        "source cell column overflow");

    expect_public_reference_boundary_materialization_failure(
        "zero-cell-row",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A0"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader found an invalid cell reference",
        "source zero row cell reference");

    expect_public_reference_boundary_materialization_failure(
        "cell-row-overflow",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1048577"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader cell row exceeds Excel limits",
        "source cell row overflow");

    expect_public_reference_boundary_materialization_failure(
        "non-column-first",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="1A"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader found an invalid cell reference",
        "source non-column-first cell reference");

    expect_public_reference_boundary_materialization_failure(
        "zero-row-number",
        worksheet_xml(
            R"(<sheetData><row r="0"><c r="A1"><v>1</v></c></row></sheetData>)"),
        "must be one-based",
        "source zero row number");

    expect_public_reference_boundary_materialization_failure(
        "row-number-overflow",
        worksheet_xml(
            R"(<sheetData><row r="1048577"><c r="A1048577"><v>1</v></c></row></sheetData>)"),
        "row exceeds Excel limits",
        "source row number overflow");

    expect_public_reference_boundary_materialization_failure(
        "invalid-row-number",
        worksheet_xml(
            R"(<sheetData><row r="bad"><c r="A1"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader row found an invalid row number",
        "source invalid row number");
}

void test_public_worksheet_editor_rejects_source_state_machine_shapes_cleanly()
{
    const auto write_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0)});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-source-state-machine")});
        writer.close();
        return source;
    };

    const auto worksheet_xml = [](std::string_view body) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + std::string(body) + "</worksheet>";
    };

    const auto expect_public_state_machine_materialization_failure =
        [&](std::string_view tag,
            std::string_view replacement_worksheet_xml,
            std::string_view expected_diagnostic,
            std::string_view scenario) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-state-machine-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-state-machine-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/worksheets/sheet1.xml") =
                std::string(replacement_worksheet_xml);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-state-machine-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(source,
                output,
                expected_diagnostic,
                replacement_text,
                scenario,
                "Untouched",
                "xl/worksheets/sheet2.xml");
        };

    expect_public_state_machine_materialization_failure(
        "row-outside-sheet-data",
        worksheet_xml(
            R"(<row r="1"><c r="A1"><v>1</v></c></row><sheetData/>)"),
        "worksheet event reader found row outside sheetData",
        "source row outside sheetData");

    expect_public_state_machine_materialization_failure(
        "worksheet-raw-text",
        worksheet_xml(
            R"(<dimension ref="A1"/>direct-worksheet-text<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader found worksheet text outside metadata or sheetData",
        "source worksheet raw text outside metadata");

    expect_public_state_machine_materialization_failure(
        "nested-rows",
        worksheet_xml(
            R"(<sheetData><row r="1"><row r="2"></row></row></sheetData>)"),
        "worksheet event reader found an invalid row boundary",
        "source nested rows");

    expect_public_state_machine_materialization_failure(
        "cell-outside-row",
        worksheet_xml(R"(<sheetData><c r="A1"><v>1</v></c></sheetData>)"),
        "worksheet event reader found cell outside row",
        "source cell outside row");

    expect_public_state_machine_materialization_failure(
        "sheet-data-raw-text",
        worksheet_xml(
            R"(<sheetData>direct-sheet-data-text<row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader found sheetData text outside a row",
        "source sheetData raw text outside rows");

    expect_public_state_machine_materialization_failure(
        "nested-cells",
        worksheet_xml(
            R"(<sheetData><row r="1"><c r="A1"><c r="B1"><v>1</v></c></c></row></sheetData>)"),
        "worksheet event reader found an invalid cell boundary",
        "source nested cells");

    expect_public_state_machine_materialization_failure(
        "row-raw-text",
        worksheet_xml(
            R"(<sheetData><row r="1">direct-row-text<c r="A1"><v>1</v></c></row></sheetData>)"),
        "CellStore worksheet loader found row text outside a cell",
        "source row raw text outside cells");
}

void test_public_worksheet_editor_rejects_source_root_boundaries_cleanly()
{
    const auto write_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0)});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-source-root-boundary")});
        writer.close();
        return source;
    };

    const auto worksheet_xml = [](std::string_view body) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + std::string(body) + "</worksheet>";
    };

    const auto expect_public_root_boundary_materialization_failure =
        [&](std::string_view tag,
            std::string_view replacement_worksheet_xml,
            std::string_view expected_diagnostic,
            std::string_view scenario) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-source-root-boundary-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-source-root-boundary-")
                + std::string(tag) + "-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/worksheets/sheet1.xml") =
                std::string(replacement_worksheet_xml);
            write_stored_zip_entries(source, entries);

            const std::string replacement_text =
                std::string("usable-after-source-root-boundary-") + std::string(tag);
            check_public_worksheet_materialization_failure_hygiene(source,
                output,
                expected_diagnostic,
                replacement_text,
                scenario,
                "Untouched",
                "xl/worksheets/sheet2.xml");
        };

    expect_public_root_boundary_materialization_failure(
        "markup-before-root",
        std::string(R"(<ignored/>)")
            + worksheet_xml(R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"),
        "worksheet event reader found markup before worksheet root",
        "source markup before worksheet root");

    expect_public_root_boundary_materialization_failure(
        "duplicate-sheet-data",
        worksheet_xml(R"(<sheetData/><sheetData/>)"),
        "worksheet event reader found an invalid sheetData boundary",
        "source duplicate sheetData elements");

    expect_public_root_boundary_materialization_failure(
        "duplicate-root",
        worksheet_xml(R"(<sheetData/>)") + std::string(R"(<worksheet><sheetData/></worksheet>)"),
        "worksheet event reader found markup after worksheet root",
        "source duplicate worksheet root");

    expect_public_root_boundary_materialization_failure(
        "text-after-root",
        worksheet_xml(R"(<sheetData/>)") + std::string("trailing-text"),
        "worksheet event reader found text after worksheet root",
        "source text after worksheet root");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_failed_materialization_keeps_noop_save_as_copy_original();
        test_public_worksheet_editor_rejects_invalid_source_shared_string_index();
        test_public_worksheet_editor_rejects_invalid_source_shared_strings_metadata();
        test_public_worksheet_editor_materializes_source_style_ids_and_rejects_malformed_style_attributes();
        test_public_worksheet_editor_rejects_unsupported_source_cell_shapes_cleanly();
        test_public_worksheet_editor_rejects_malformed_source_worksheet_xml_cleanly();
        test_public_worksheet_editor_rejects_source_cell_reference_issues_cleanly();
        test_public_worksheet_editor_rejects_source_formula_shapes_cleanly();
        test_public_worksheet_editor_rejects_source_inline_text_shapes_cleanly();
        test_public_worksheet_editor_rejects_source_row_cell_structure_cleanly();
        test_public_worksheet_editor_rejects_source_value_wrapper_shapes_cleanly();
        test_public_worksheet_editor_rejects_wrong_namespace_unsupported_local_names_cleanly();
        test_public_worksheet_editor_rejects_source_xml_parser_issues_cleanly();
        test_public_worksheet_editor_rejects_source_reference_boundaries_cleanly();
        test_public_worksheet_editor_rejects_source_state_machine_shapes_cleanly();
        test_public_worksheet_editor_rejects_source_root_boundaries_cleanly();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor source-failure check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor source-failure tests passed\n");
    return 0;
}
