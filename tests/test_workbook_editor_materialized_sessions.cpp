// WorkbookEditor materialized-session internal structure tests.
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

void check_internal_materialized_flush_failure_dirty_projection_state(
    const fastxlsx::WorkbookEditor& editor,
    std::string_view scenario)
{
    const std::string label = std::string(scenario);

    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_names(
              editor) == std::vector<std::string>{"Data", "Ghost"},
        label + " should preserve all internal dirty session names");
    check(editor.has_pending_changes(),
        label + " should keep save_as pending");
    check(editor.pending_change_count() == 0,
        label + " should not publish coarse public edit handoffs");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"Data"},
        label + " should expose only catalog-backed dirty materialized names");
    check(editor.pending_materialized_cell_count() == 5,
        label + " should preserve aggregate dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() > 0,
        label + " should preserve aggregate dirty materialized memory");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        label + " should expose one catalog-backed dirty summary");
    if (summaries.size() == 1) {
        const fastxlsx::WorkbookEditorWorksheetEditSummary& summary = summaries[0];
        check(summary.source_name == "Data" &&
                summary.planned_name == "Data" &&
                !summary.renamed &&
                !summary.sheet_data_replaced &&
                !summary.targeted_cells_replaced &&
                summary.materialized_dirty &&
                summary.materialized_cell_count == 3 &&
                summary.estimated_materialized_memory_usage > 0,
            label + " should preserve only the valid Data dirty summary");
    }
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

std::filesystem::path write_two_sheet_source_with_shift_formula(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2"),
            fastxlsx::CellView::text("row2-gap-b2"),
            fastxlsx::CellView::formula("A1+B1")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_delete_shift_formula(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("delete-a1"),
            fastxlsx::CellView::text("delete-b1")});
        data.append_row({fastxlsx::CellView::text("delete-a2"),
            fastxlsx::CellView::text("ref-b2"),
            fastxlsx::CellView::text("ref-c2"),
            fastxlsx::CellView::formula("B2+C2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_stationary_formula(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("B1+B2"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("row2-gap-a2"),
            fastxlsx::CellView::text("ref-b2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_absolute_stationary_formula(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("B$2+$B2+$B$2+B1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("row2-gap-a2"),
            fastxlsx::CellView::text("absolute-ref-b2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_stationary_range_formula(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("SUM(B1:B2)+B:B+2:2"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("row2-gap-a2"),
            fastxlsx::CellView::text("range-ref-b2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_stationary_range_row_delete_formula(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("SUM(B2:B3)+B:B+2:3"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("delete-a2"),
            fastxlsx::CellView::text("delete-b2")});
        data.append_row({fastxlsx::CellView::text("row3-gap-a3"),
            fastxlsx::CellView::text("range-ref-b3")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_stationary_range_column_delete_formula(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("SUM(B1:C1)+B:C+1:1"),
            fastxlsx::CellView::text("delete-b1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("row2-gap-a2"),
            fastxlsx::CellView::text("delete-b2"),
            fastxlsx::CellView::text("range-ref-c2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_absolute_row_delete_formula(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("B$2+$B3+$B$3+B1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("delete-a2"),
            fastxlsx::CellView::text("delete-b2")});
        data.append_row({fastxlsx::CellView::text("row3-gap-a3"),
            fastxlsx::CellView::text("absolute-ref-b3")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_absolute_column_delete_formula(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("B$2+$C2+$C$2+A1"),
            fastxlsx::CellView::text("delete-b1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("row2-gap-a2"),
            fastxlsx::CellView::text("delete-b2"),
            fastxlsx::CellView::text("absolute-ref-c2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_stationary_delete_formula(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("C1+C2"),
            fastxlsx::CellView::text("delete-b1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("row2-gap-a2"),
            fastxlsx::CellView::text("delete-b2"),
            fastxlsx::CellView::text("ref-c2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_stationary_row_delete_formula(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::formula("B2+B3"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("delete-a2"),
            fastxlsx::CellView::text("delete-b2")});
        data.append_row({fastxlsx::CellView::text("row3-gap-a3"),
            fastxlsx::CellView::text("ref-b3")});
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

void test_public_worksheet_editor_zero_count_shifts_clear_diagnostics_preserve_state()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-zero-count-shifts-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-zero-count-shifts-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t clean_cell_count = sheet.cell_count();
    const std::size_t clean_memory = sheet.estimated_memory_usage();
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid"));
    }), "zero-count public shift setup should record a clean-session diagnostic");
    check(editor.last_edit_error().has_value(),
        "zero-count public shift setup should expose the clean-session diagnostic");

    sheet.insert_rows(2, 0);
    sheet.delete_rows(2, 0);
    sheet.insert_columns(2, 0);
    sheet.delete_columns(2, 0);

    check(!editor.last_edit_error().has_value(),
        "zero-count public shifts should clear clean-session diagnostics");
    check(!editor.has_pending_changes(),
        "zero-count public shifts should keep a clean editor clean");
    check(!sheet.has_pending_changes(),
        "zero-count public shifts should keep a clean handle clean");
    check(sheet.cell_count() == clean_cell_count,
        "zero-count public shifts should preserve clean sparse count");
    check(sheet.estimated_memory_usage() == clean_memory,
        "zero-count public shifts should preserve clean sparse memory");
    const std::optional<fastxlsx::CellValue> clean_a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> clean_b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> clean_a2 = sheet.try_cell("A2");
    check(clean_a1.has_value() && clean_a1->kind() == fastxlsx::CellValueKind::Text &&
            clean_a1->text_value() == "placeholder-a1",
        "zero-count public shifts should preserve clean A1");
    check(clean_b1.has_value() && clean_b1->kind() == fastxlsx::CellValueKind::Number &&
            clean_b1->number_value() == 1.0,
        "zero-count public shifts should preserve clean B1");
    check(clean_a2.has_value() && clean_a2->kind() == fastxlsx::CellValueKind::Text &&
            clean_a2->text_value() == "placeholder-a2",
        "zero-count public shifts should preserve clean A2");

    sheet.set_cell(3, 3, fastxlsx::CellValue::text("dirty-c3"));
    const std::size_t dirty_memory = sheet.estimated_memory_usage();
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "zero-count public shift dirty setup should dirty the materialized session");
    check(threw_fastxlsx_error([&] { sheet.delete_columns(0, 1); }),
        "zero-count public shift dirty setup should record a dirty-session diagnostic");
    check(editor.last_edit_error().has_value(),
        "zero-count public shift dirty setup should expose the dirty-session diagnostic");

    sheet.insert_rows(2, 0);
    sheet.delete_rows(2, 0);
    sheet.insert_columns(2, 0);
    sheet.delete_columns(2, 0);

    check(!editor.last_edit_error().has_value(),
        "zero-count public shifts should clear dirty-session diagnostics");
    check(sheet.has_pending_changes(),
        "zero-count public shifts should keep a dirty handle dirty");
    check(editor.pending_change_count() == 0,
        "zero-count public shifts should not queue coarse public edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "zero-count public shifts should preserve dirty materialized names");
    check(editor.pending_materialized_cell_count() == clean_cell_count + 1U,
        "zero-count public shifts should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
        "zero-count public shifts should preserve dirty materialized memory");
    const std::optional<fastxlsx::CellValue> dirty_c3 = sheet.try_cell("C3");
    check(dirty_c3.has_value() && dirty_c3->kind() == fastxlsx::CellValueKind::Text &&
            dirty_c3->text_value() == "dirty-c3",
        "zero-count public shifts should preserve dirty C3");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "zero-count public shift save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "dirty-c3",
        "zero-count public shift save should persist the dirty sparse cell");
    check(!sheet.has_pending_changes(),
        "zero-count public shift save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "zero-count public shift save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "zero-count public shift save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "zero-count public shift save should clear dirty materialized memory");
}

void test_public_worksheet_editor_disjoint_shifts_clear_diagnostics_preserve_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-disjoint-shifts-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-disjoint-shifts-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("dirty-c3"));
    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory = sheet.estimated_memory_usage();

    check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
        "disjoint public shift setup should record a dirty-session diagnostic");
    check(editor.last_edit_error().has_value(),
        "disjoint public shift setup should expose the dirty-session diagnostic");

    sheet.insert_rows(10, 1);
    sheet.delete_rows(10, 1);
    sheet.insert_columns(10, 1);
    sheet.delete_columns(10, 1);

    check(!editor.last_edit_error().has_value(),
        "disjoint public shifts should clear dirty-session diagnostics");
    check(sheet.has_pending_changes(),
        "disjoint public shifts should keep the dirty handle dirty");
    check(editor.pending_change_count() == 0,
        "disjoint public shifts should not queue coarse public edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "disjoint public shifts should preserve dirty materialized names");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "disjoint public shifts should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
        "disjoint public shifts should preserve dirty materialized memory");
    check(sheet.cell_count() == dirty_cell_count,
        "disjoint public shifts should preserve sparse count");
    check(sheet.estimated_memory_usage() == dirty_memory,
        "disjoint public shifts should preserve sparse memory");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    const std::optional<fastxlsx::CellValue> c3 = sheet.try_cell("C3");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text &&
            a1->text_value() == "placeholder-a1",
        "disjoint public shifts should preserve A1");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Number &&
            b1->number_value() == 1.0,
        "disjoint public shifts should preserve B1");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Text &&
            a2->text_value() == "placeholder-a2",
        "disjoint public shifts should preserve A2");
    check(c3.has_value() && c3->kind() == fastxlsx::CellValueKind::Text &&
            c3->text_value() == "dirty-c3",
        "disjoint public shifts should preserve C3");
    check(!sheet.try_cell("D4").has_value(),
        "disjoint public shifts should not leave shifted cells behind");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "disjoint public shift save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "dirty-c3",
        "disjoint public shift save should persist the dirty sparse cell");
    check_not_contains(worksheet_xml, "D4",
        "disjoint public shift save should not persist shifted coordinates");
    check(!sheet.has_pending_changes(),
        "disjoint public shift save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "disjoint public shift save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "disjoint public shift save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "disjoint public shift save should clear dirty materialized memory");
}

void test_public_worksheet_editor_boundary_shifts_clear_diagnostics_preserve_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-boundary-shifts-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-boundary-shifts-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("dirty-c3"));
    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory = sheet.estimated_memory_usage();

    check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
        "boundary public shift setup should record a dirty-session diagnostic");
    check(editor.last_edit_error().has_value(),
        "boundary public shift setup should expose the dirty-session diagnostic");

    sheet.insert_rows(1048576, 1);
    sheet.delete_rows(1048576, 1);
    sheet.insert_columns(16384, 1);
    sheet.delete_columns(16384, 1);

    check(!editor.last_edit_error().has_value(),
        "boundary public shifts should clear dirty-session diagnostics");
    check(sheet.has_pending_changes(),
        "boundary public shifts should keep the dirty handle dirty");
    check(editor.pending_change_count() == 0,
        "boundary public shifts should not queue coarse public edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "boundary public shifts should preserve dirty materialized names");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "boundary public shifts should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
        "boundary public shifts should preserve dirty materialized memory");
    check(sheet.cell_count() == dirty_cell_count,
        "boundary public shifts should preserve sparse count");
    check(sheet.estimated_memory_usage() == dirty_memory,
        "boundary public shifts should preserve sparse memory");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    const std::optional<fastxlsx::CellValue> c3 = sheet.try_cell("C3");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text &&
            a1->text_value() == "placeholder-a1",
        "boundary public shifts should preserve A1");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Number &&
            b1->number_value() == 1.0,
        "boundary public shifts should preserve B1");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Text &&
            a2->text_value() == "placeholder-a2",
        "boundary public shifts should preserve A2");
    check(c3.has_value() && c3->kind() == fastxlsx::CellValueKind::Text &&
            c3->text_value() == "dirty-c3",
        "boundary public shifts should preserve C3");
    check(!sheet.try_cell("A1048576").has_value() &&
            !sheet.try_cell("XFD1").has_value() &&
            !sheet.try_cell("D4").has_value(),
        "boundary public shifts should not synthesize edge or shifted cells");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three = sheet.row_cells(3);
    check(row_three.size() == 1 &&
            row_three[0].reference.row == 3 &&
            row_three[0].reference.column == 3 &&
            row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_three[0].value.text_value() == "dirty-c3",
        "boundary public shifts should preserve row_cells for the dirty row");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three = sheet.column_cells(3);
    check(column_three.size() == 1 &&
            column_three[0].reference.row == 3 &&
            column_three[0].reference.column == 3 &&
            column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            column_three[0].value.text_value() == "dirty-c3",
        "boundary public shifts should preserve column_cells for the dirty column");
    check(sheet.row_cells(1048576).empty() && sheet.column_cells(16384).empty(),
        "boundary public shifts should keep edge row and column snapshots empty");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "boundary public shift save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "dirty-c3",
        "boundary public shift save should persist the dirty sparse cell");
    check_not_contains(worksheet_xml, "A1048576",
        "boundary public shift save should not persist row-edge coordinates");
    check_not_contains(worksheet_xml, "XFD1",
        "boundary public shift save should not persist column-edge coordinates");
    check_not_contains(worksheet_xml, "D4",
        "boundary public shift save should not persist shifted coordinates");
    check(!sheet.has_pending_changes(),
        "boundary public shift save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "boundary public shift save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "boundary public shift save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "boundary public shift save should clear dirty materialized memory");
}

void test_public_worksheet_editor_invalid_shifts_preserve_dirty_state_and_recover()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-invalid-shifts-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-invalid-shifts-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("dirty-c3"));
    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory = sheet.estimated_memory_usage();

    const auto check_dirty_state_preserved = [&](std::string_view label) {
        check(editor.last_edit_error().has_value(),
            std::string(label) + " should leave a public edit diagnostic");
        check(sheet.has_pending_changes(),
            std::string(label) + " should keep the dirty handle dirty");
        check(editor.pending_change_count() == 0,
            std::string(label) + " should not queue coarse public edits before save");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            std::string(label) + " should preserve dirty materialized names");
        check(editor.pending_materialized_cell_count() == dirty_cell_count,
            std::string(label) + " should preserve dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
            std::string(label) + " should preserve dirty materialized memory");
        check(sheet.cell_count() == dirty_cell_count,
            std::string(label) + " should preserve sparse count");
        check(sheet.estimated_memory_usage() == dirty_memory,
            std::string(label) + " should preserve sparse memory");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "placeholder-a2" &&
                sheet.get_cell("C3").text_value() == "dirty-c3",
            std::string(label) + " should preserve source and dirty cells");
        check(!sheet.try_cell("D4").has_value() &&
                !sheet.try_cell("A1048576").has_value() &&
                !sheet.try_cell("XFD1").has_value(),
            std::string(label) + " should not synthesize shifted or edge cells");
        const std::vector<fastxlsx::WorksheetCellSnapshot> row_three = sheet.row_cells(3);
        check(row_three.size() == 1 &&
                row_three[0].reference.row == 3 &&
                row_three[0].reference.column == 3 &&
                row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                row_three[0].value.text_value() == "dirty-c3",
            std::string(label) + " should preserve row snapshot order");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
            sheet.column_cells(3);
        check(column_three.size() == 1 &&
                column_three[0].reference.row == 3 &&
                column_three[0].reference.column == 3 &&
                column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                column_three[0].value.text_value() == "dirty-c3",
            std::string(label) + " should preserve column snapshot order");
    };

    check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
        "invalid public shift should reject row zero insertion");
    check_dirty_state_preserved("row zero insertion failure");

    check(threw_fastxlsx_error([&] { sheet.delete_rows(1048576, 2); }),
        "invalid public shift should reject row count past the Excel limit");
    check_dirty_state_preserved("row count overflow failure");

    check(threw_fastxlsx_error([&] { sheet.insert_columns(0, 1); }),
        "invalid public shift should reject column zero insertion");
    check_dirty_state_preserved("column zero insertion failure");

    check(threw_fastxlsx_error([&] { sheet.delete_columns(16384, 2); }),
        "invalid public shift should reject column count past the Excel limit");
    check_dirty_state_preserved("column count overflow failure");

    sheet.insert_rows(10, 1);
    sheet.delete_rows(10, 1);
    sheet.insert_columns(10, 1);
    sheet.delete_columns(10, 1);

    check(!editor.last_edit_error().has_value(),
        "valid public shift no-ops should clear invalid shift diagnostics");
    check(sheet.has_pending_changes(),
        "valid public shift no-ops should keep the recovered dirty handle dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "valid public shift no-ops should keep recovered dirty materialized names");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "valid public shift no-ops should keep recovered dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
        "valid public shift no-ops should keep recovered dirty materialized memory");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid public shift recovery save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "dirty-c3",
        "invalid public shift recovery save should persist the dirty sparse cell");
    check_not_contains(worksheet_xml, "D4",
        "invalid public shift recovery save should not persist shifted coordinates");
    check_not_contains(worksheet_xml, "A1048576",
        "invalid public shift recovery save should not persist row-edge coordinates");
    check_not_contains(worksheet_xml, "XFD1",
        "invalid public shift recovery save should not persist column-edge coordinates");
    check(!sheet.has_pending_changes(),
        "invalid public shift recovery save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "invalid public shift recovery save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "invalid public shift recovery save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "invalid public shift recovery save should clear dirty materialized memory");
}

void test_public_worksheet_editor_shifted_state_survives_invalid_shift_recovery()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-shifted-invalid-recovery-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-shifted-invalid-recovery-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("dirty-c3"));

    sheet.insert_rows(2, 1);
    sheet.insert_columns(2, 1);
    const std::size_t shifted_cell_count = sheet.cell_count();
    const std::size_t shifted_memory = sheet.estimated_memory_usage();

    const auto check_shifted_state_preserved = [&](std::string_view label) {
        check(sheet.has_pending_changes(),
            std::string(label) + " should keep the shifted handle dirty");
        check(editor.pending_change_count() == 0,
            std::string(label) + " should not queue coarse public edits before save");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            std::string(label) + " should preserve shifted materialized names");
        check(editor.pending_materialized_cell_count() == shifted_cell_count,
            std::string(label) + " should preserve shifted materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            std::string(label) + " should preserve shifted materialized memory");
        check(sheet.cell_count() == shifted_cell_count,
            std::string(label) + " should preserve shifted sparse count");
        check(sheet.estimated_memory_usage() == shifted_memory,
            std::string(label) + " should preserve shifted sparse memory");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                sheet.get_cell("C1").number_value() == 1.0 &&
                sheet.get_cell("A3").text_value() == "placeholder-a2" &&
                sheet.get_cell("D4").text_value() == "dirty-c3",
            std::string(label) + " should preserve shifted source and dirty cells");
        check(!sheet.try_cell("B1").has_value() &&
                !sheet.try_cell("A2").has_value() &&
                !sheet.try_cell("C3").has_value(),
            std::string(label) + " should keep pre-shift coordinates empty");

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                row_one[0].value.text_value() == "placeholder-a1" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 3 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            std::string(label) + " should preserve shifted row one snapshot order");
        const std::vector<fastxlsx::WorksheetCellSnapshot> row_four = sheet.row_cells(4);
        check(row_four.size() == 1 &&
                row_four[0].reference.row == 4 &&
                row_four[0].reference.column == 4 &&
                row_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                row_four[0].value.text_value() == "dirty-c3",
            std::string(label) + " should preserve shifted dirty row snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
            sheet.column_cells(4);
        check(column_four.size() == 1 &&
                column_four[0].reference.row == 4 &&
                column_four[0].reference.column == 4 &&
                column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                column_four[0].value.text_value() == "dirty-c3",
            std::string(label) + " should preserve shifted dirty column snapshot");
    };

    check(!editor.last_edit_error().has_value(),
        "successful public shifts should leave diagnostics clear");
    check_shifted_state_preserved("successful public shifts");

    check(threw_fastxlsx_error([&] { sheet.insert_rows(1048576, 2); }),
        "shifted invalid public recovery should reject row count past the Excel limit");
    check(editor.last_edit_error().has_value(),
        "shifted invalid row count failure should leave a public edit diagnostic");
    check_shifted_state_preserved("shifted invalid row count failure");

    check(threw_fastxlsx_error([&] { sheet.delete_columns(0, 1); }),
        "shifted invalid public recovery should reject column zero deletion");
    check(editor.last_edit_error().has_value(),
        "shifted invalid column zero failure should leave a public edit diagnostic");
    check_shifted_state_preserved("shifted invalid column zero failure");

    sheet.insert_rows(10, 1);
    sheet.delete_rows(10, 1);
    sheet.insert_columns(10, 1);
    sheet.delete_columns(10, 1);

    check(!editor.last_edit_error().has_value(),
        "shifted valid no-ops should clear invalid shift diagnostics");
    check_shifted_state_preserved("shifted valid no-op recovery");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shifted invalid recovery save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "shifted invalid recovery save should persist shifted bounds");
    check_contains(worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "shifted invalid recovery save should persist shifted source number");
    check_contains(worksheet_xml,
        R"(<c r="D4" t="inlineStr"><is><t>dirty-c3</t></is></c>)",
        "shifted invalid recovery save should persist shifted dirty cell");
    check_contains(worksheet_xml, "placeholder-a2",
        "shifted invalid recovery save should persist shifted source text");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "shifted invalid recovery save should not persist pre-shift B1");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "shifted invalid recovery save should not persist pre-shift A2");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "shifted invalid recovery save should not persist pre-shift dirty coordinate");
    check(!sheet.has_pending_changes(),
        "shifted invalid recovery save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "shifted invalid recovery save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "shifted invalid recovery save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "shifted invalid recovery save should clear dirty materialized memory");
}

void test_public_worksheet_editor_delete_rows_columns_project_sparse_state()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-delete-shifts-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-delete-shifts-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("dirty-c3"));
    sheet.set_cell(2, 4, fastxlsx::CellValue::text("tail-d2"));

    sheet.delete_rows(1, 1);
    sheet.delete_columns(2, 1);
    const std::size_t projected_cell_count = sheet.cell_count();
    const std::size_t projected_memory = sheet.estimated_memory_usage();

    check(!editor.last_edit_error().has_value(),
        "public delete row/column shifts should leave diagnostics clear");
    check(sheet.has_pending_changes(),
        "public delete row/column shifts should keep the handle dirty");
    check(editor.pending_change_count() == 0,
        "public delete row/column shifts should not queue coarse edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "public delete row/column shifts should keep dirty materialized names");
    check(editor.pending_materialized_cell_count() == projected_cell_count,
        "public delete row/column shifts should report projected materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
        "public delete row/column shifts should report projected materialized memory");
    check(projected_cell_count == 3,
        "public delete row/column shifts should remove deleted sparse records");

    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("C1").text_value() == "tail-d2" &&
            sheet.get_cell("B2").text_value() == "dirty-c3",
        "public delete row/column shifts should project surviving source and dirty cells");
    check(!sheet.try_cell("A2").has_value() &&
            !sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("C2").has_value() &&
            !sheet.try_cell("D1").has_value(),
        "public delete row/column shifts should clear deleted and pre-shift coordinates");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == "placeholder-a2" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 3 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[1].value.text_value() == "tail-d2",
        "public delete row/column shifts should preserve row snapshot order");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two = sheet.row_cells(2);
    check(row_two.size() == 1 &&
            row_two[0].reference.row == 2 &&
            row_two[0].reference.column == 2 &&
            row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_two[0].value.text_value() == "dirty-c3",
        "public delete row/column shifts should expose shifted dirty row snapshot");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_two = sheet.column_cells(2);
    check(column_two.size() == 1 &&
            column_two[0].reference.row == 2 &&
            column_two[0].reference.column == 2 &&
            column_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            column_two[0].value.text_value() == "dirty-c3",
        "public delete row/column shifts should expose shifted dirty column snapshot");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "public delete row/column shift save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "public delete row/column shift save should persist projected bounds");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "public delete row/column shift save should persist shifted source text");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>tail-d2</t></is></c>)",
        "public delete row/column shift save should persist shifted tail text");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>dirty-c3</t></is></c>)",
        "public delete row/column shift save should persist shifted dirty cell");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "public delete row/column shift save should omit deleted source text");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "public delete row/column shift save should omit old source row coordinate");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "public delete row/column shift save should omit old source number coordinate");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "public delete row/column shift save should omit old dirty coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "public delete row/column shift save should preserve untouched worksheets");
    check(!sheet.has_pending_changes(),
        "public delete row/column shift save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "public delete row/column shift save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "public delete row/column shift save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "public delete row/column shift save should clear dirty materialized memory");
}

void test_public_worksheet_editor_insert_rows_columns_project_sparse_state()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-insert-shifts-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-insert-shifts-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("dirty-c3"));
    sheet.set_cell(2, 4, fastxlsx::CellValue::text("tail-d2"));

    sheet.insert_rows(2, 1);
    sheet.insert_columns(2, 1);
    const std::size_t projected_cell_count = sheet.cell_count();
    const std::size_t projected_memory = sheet.estimated_memory_usage();

    check(!editor.last_edit_error().has_value(),
        "public insert row/column shifts should leave diagnostics clear");
    check(sheet.has_pending_changes(),
        "public insert row/column shifts should keep the handle dirty");
    check(editor.pending_change_count() == 0,
        "public insert row/column shifts should not queue coarse edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "public insert row/column shifts should keep dirty materialized names");
    check(editor.pending_materialized_cell_count() == projected_cell_count,
        "public insert row/column shifts should report projected materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
        "public insert row/column shifts should report projected materialized memory");
    check(projected_cell_count == 5,
        "public insert row/column shifts should preserve sparse record count");

    check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("E3").text_value() == "tail-d2" &&
            sheet.get_cell("D4").text_value() == "dirty-c3",
        "public insert row/column shifts should project source and dirty cells");
    check(!sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() &&
            !sheet.try_cell("C3").has_value() &&
            !sheet.try_cell("D2").has_value(),
        "public insert row/column shifts should leave inserted gaps and old coordinates empty");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == "placeholder-a1" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 3 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        "public insert row/column shifts should preserve row one snapshot order");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three = sheet.row_cells(3);
    check(row_three.size() == 2 &&
            row_three[0].reference.row == 3 &&
            row_three[0].reference.column == 1 &&
            row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_three[0].value.text_value() == "placeholder-a2" &&
            row_three[1].reference.row == 3 &&
            row_three[1].reference.column == 5 &&
            row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
            row_three[1].value.text_value() == "tail-d2",
        "public insert row/column shifts should preserve shifted row snapshot order");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four = sheet.column_cells(4);
    check(column_four.size() == 1 &&
            column_four[0].reference.row == 4 &&
            column_four[0].reference.column == 4 &&
            column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
            column_four[0].value.text_value() == "dirty-c3",
        "public insert row/column shifts should expose shifted dirty column snapshot");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "public insert row/column shift save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E4"/>)",
        "public insert row/column shift save should persist projected bounds");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>placeholder-a1</t></is></c>)",
        "public insert row/column shift save should preserve source A1");
    check_contains(worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "public insert row/column shift save should persist shifted source number");
    check_contains(worksheet_xml,
        R"(<c r="A3" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "public insert row/column shift save should persist shifted source row");
    check_contains(worksheet_xml,
        R"(<c r="E3" t="inlineStr"><is><t>tail-d2</t></is></c>)",
        "public insert row/column shift save should persist shifted tail text");
    check_contains(worksheet_xml,
        R"(<c r="D4" t="inlineStr"><is><t>dirty-c3</t></is></c>)",
        "public insert row/column shift save should persist shifted dirty cell");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "public insert row/column shift save should omit inserted column gap");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "public insert row/column shift save should omit inserted row gap");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "public insert row/column shift save should omit old dirty coordinate");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "public insert row/column shift save should omit old tail coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "public insert row/column shift save should preserve untouched worksheets");
    check(!sheet.has_pending_changes(),
        "public insert row/column shift save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "public insert row/column shift save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "public insert row/column shift save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "public insert row/column shift save should clear dirty materialized memory");
}

void test_public_worksheet_editor_insert_rows_columns_translate_moved_formula()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_shift_formula(
            "fastxlsx-workbook-editor-materialized-insert-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-insert-formula-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    sheet.insert_columns(2, 1);
    const std::size_t projected_cell_count = sheet.cell_count();
    const std::size_t projected_memory = sheet.estimated_memory_usage();

    check(!editor.last_edit_error().has_value(),
        "public insert formula shift should leave diagnostics clear");
    check(sheet.has_pending_changes(),
        "public insert formula shift should dirty the borrowed handle");
    check(editor.pending_change_count() == 0,
        "public insert formula shift should not queue coarse edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "public insert formula shift should keep dirty materialized names");
    check(editor.pending_materialized_cell_count() == projected_cell_count,
        "public insert formula shift should report projected materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
        "public insert formula shift should report projected materialized memory");
    check(projected_cell_count == 5,
        "public insert formula shift should preserve sparse record count");

    const fastxlsx::CellValue shifted_formula = sheet.get_cell("D3");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula.text_value() == "B2+C2",
        "public insert formula shift should translate moved formula references");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("C3").text_value() == "row2-gap-b2",
        "public insert formula shift should preserve shifted source cells");
    check(!sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() &&
            !sheet.try_cell("C2").has_value(),
        "public insert formula shift should leave gaps and old formula coordinate empty");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three = sheet.row_cells(3);
    check(row_three.size() == 3 &&
            row_three[0].reference.row == 3 &&
            row_three[0].reference.column == 1 &&
            row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_three[0].value.text_value() == "placeholder-a2" &&
            row_three[1].reference.row == 3 &&
            row_three[1].reference.column == 3 &&
            row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
            row_three[1].value.text_value() == "row2-gap-b2" &&
            row_three[2].reference.row == 3 &&
            row_three[2].reference.column == 4 &&
            row_three[2].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_three[2].value.text_value() == "B2+C2",
        "public insert formula shift should expose translated formula in row snapshots");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four = sheet.column_cells(4);
    check(column_four.size() == 1 &&
            column_four[0].reference.row == 3 &&
            column_four[0].reference.column == 4 &&
            column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            column_four[0].value.text_value() == "B2+C2",
        "public insert formula shift should expose translated formula in column snapshots");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "public insert formula shift save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "public insert formula shift save should persist projected bounds");
    check_contains(worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "public insert formula shift save should persist shifted source number");
    check_contains(worksheet_xml,
        R"(<c r="D3"><f>B2+C2</f></c>)",
        "public insert formula shift save should persist translated moved formula");
    check_contains(worksheet_xml, "placeholder-a2",
        "public insert formula shift save should persist shifted source text");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "public insert formula shift save should omit inserted column gap");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "public insert formula shift save should omit inserted row gap");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "public insert formula shift save should omit old formula coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "public insert formula shift save should preserve untouched worksheets");
    check(!sheet.has_pending_changes(),
        "public insert formula shift save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "public insert formula shift save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "public insert formula shift save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "public insert formula shift save should clear dirty materialized memory");
}

void test_public_worksheet_editor_delete_rows_columns_translate_moved_formula()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_delete_shift_formula(
            "fastxlsx-workbook-editor-materialized-delete-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-delete-formula-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_rows(1, 1);
    sheet.delete_columns(1, 1);
    const std::size_t projected_cell_count = sheet.cell_count();
    const std::size_t projected_memory = sheet.estimated_memory_usage();

    check(!editor.last_edit_error().has_value(),
        "public delete formula shift should leave diagnostics clear");
    check(sheet.has_pending_changes(),
        "public delete formula shift should dirty the borrowed handle");
    check(editor.pending_change_count() == 0,
        "public delete formula shift should not queue coarse edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "public delete formula shift should keep dirty materialized names");
    check(editor.pending_materialized_cell_count() == projected_cell_count,
        "public delete formula shift should report projected materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
        "public delete formula shift should report projected materialized memory");
    check(projected_cell_count == 3,
        "public delete formula shift should remove deleted sparse records");

    const fastxlsx::CellValue shifted_formula = sheet.get_cell("C1");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula.text_value() == "A1+B1",
        "public delete formula shift should translate moved formula references");
    check(sheet.get_cell("A1").text_value() == "ref-b2" &&
            sheet.get_cell("B1").text_value() == "ref-c2",
        "public delete formula shift should preserve shifted source cells");
    check(!sheet.try_cell("A2").has_value() &&
            !sheet.try_cell("B2").has_value() &&
            !sheet.try_cell("D2").has_value(),
        "public delete formula shift should remove deleted and old formula coordinates");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 3 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == "ref-b2" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[1].value.text_value() == "ref-c2" &&
            row_one[2].reference.row == 1 &&
            row_one[2].reference.column == 3 &&
            row_one[2].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_one[2].value.text_value() == "A1+B1",
        "public delete formula shift should expose translated formula in row snapshots");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three = sheet.column_cells(3);
    check(column_three.size() == 1 &&
            column_three[0].reference.row == 1 &&
            column_three[0].reference.column == 3 &&
            column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            column_three[0].value.text_value() == "A1+B1",
        "public delete formula shift should expose translated formula in column snapshots");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "public delete formula shift save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C1"/>)",
        "public delete formula shift save should persist projected bounds");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>ref-b2</t></is></c>)",
        "public delete formula shift save should persist shifted first source cell");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>ref-c2</t></is></c>)",
        "public delete formula shift save should persist shifted second source cell");
    check_contains(worksheet_xml,
        R"(<c r="C1"><f>A1+B1</f></c>)",
        "public delete formula shift save should persist translated moved formula");
    check_not_contains(worksheet_xml, "delete-a1",
        "public delete formula shift save should omit deleted row text");
    check_not_contains(worksheet_xml, "delete-a2",
        "public delete formula shift save should omit deleted column text");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "public delete formula shift save should omit old formula coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "public delete formula shift save should preserve untouched worksheets");
    check(!sheet.has_pending_changes(),
        "public delete formula shift save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "public delete formula shift save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "public delete formula shift save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "public delete formula shift save should clear dirty materialized memory");
}

void test_public_worksheet_editor_insert_columns_rewrites_stationary_formula()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-materialized-stationary-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-stationary-formula-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_columns(2, 1);
    const std::size_t projected_cell_count = sheet.cell_count();
    const std::size_t projected_memory = sheet.estimated_memory_usage();

    check(!editor.last_edit_error().has_value(),
        "public stationary formula insert_columns should leave diagnostics clear");
    check(sheet.has_pending_changes(),
        "public stationary formula insert_columns should dirty the borrowed handle");
    check(editor.pending_change_count() == 0,
        "public stationary formula insert_columns should not queue coarse edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "public stationary formula insert_columns should keep dirty materialized names");
    check(editor.pending_materialized_cell_count() == projected_cell_count,
        "public stationary formula insert_columns should report projected materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
        "public stationary formula insert_columns should report projected memory");
    check(projected_cell_count == 4,
        "public stationary formula insert_columns should preserve sparse record count");

    const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
    check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
            stationary_formula.text_value() == "C1+C2",
        "public stationary formula insert_columns should rewrite affected references");
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("A2").text_value() == "row2-gap-a2" &&
            sheet.get_cell("C2").text_value() == "ref-b2",
        "public stationary formula insert_columns should shift referenced source cells");
    check(!sheet.try_cell("B1").has_value() && !sheet.try_cell("B2").has_value(),
        "public stationary formula insert_columns should leave inserted column gaps empty");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_one[0].value.text_value() == "C1+C2" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 3 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        "public stationary formula insert_columns should expose rewritten formula in row snapshot");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one = sheet.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            column_one[0].value.text_value() == "C1+C2" &&
            column_one[1].reference.row == 2 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "row2-gap-a2",
        "public stationary formula insert_columns should expose rewritten formula in column snapshot");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "public stationary formula insert_columns save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "public stationary formula insert_columns save should persist projected bounds");
    check_contains(worksheet_xml,
        R"(<c r="A1"><f>C1+C2</f></c>)",
        "public stationary formula insert_columns save should persist rewritten formula");
    check_contains(worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "public stationary formula insert_columns save should persist shifted source number");
    check_contains(worksheet_xml,
        R"(<c r="C2" t="inlineStr"><is><t>ref-b2</t></is></c>)",
        "public stationary formula insert_columns save should persist shifted source text");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "public stationary formula insert_columns save should omit inserted B1 gap");
    check_not_contains(worksheet_xml, R"(r="B2")",
        "public stationary formula insert_columns save should omit inserted B2 gap");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "public stationary formula insert_columns save should preserve untouched worksheets");
    check(!sheet.has_pending_changes(),
        "public stationary formula insert_columns save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "public stationary formula insert_columns save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "public stationary formula insert_columns save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "public stationary formula insert_columns save should clear dirty materialized memory");
}

void test_public_worksheet_editor_insert_rows_rewrites_stationary_formula()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-materialized-stationary-row-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-stationary-row-formula-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    const std::size_t projected_cell_count = sheet.cell_count();
    const std::size_t projected_memory = sheet.estimated_memory_usage();

    check(!editor.last_edit_error().has_value(),
        "public stationary formula insert_rows should leave diagnostics clear");
    check(sheet.has_pending_changes(),
        "public stationary formula insert_rows should dirty the borrowed handle");
    check(editor.pending_change_count() == 0,
        "public stationary formula insert_rows should not queue coarse edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "public stationary formula insert_rows should keep dirty materialized names");
    check(editor.pending_materialized_cell_count() == projected_cell_count,
        "public stationary formula insert_rows should report projected materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
        "public stationary formula insert_rows should report projected memory");
    check(projected_cell_count == 4,
        "public stationary formula insert_rows should preserve sparse record count");

    const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
    check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
            stationary_formula.text_value() == "B1+B3",
        "public stationary formula insert_rows should rewrite affected references");
    check(sheet.get_cell("B1").number_value() == 1.0 &&
            sheet.get_cell("A3").text_value() == "row2-gap-a2" &&
            sheet.get_cell("B3").text_value() == "ref-b2",
        "public stationary formula insert_rows should shift referenced source cells");
    check(!sheet.try_cell("A2").has_value() && !sheet.try_cell("B2").has_value(),
        "public stationary formula insert_rows should leave inserted row gaps empty");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_one[0].value.text_value() == "B1+B3" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        "public stationary formula insert_rows should expose rewritten formula in row snapshot");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one = sheet.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            column_one[0].value.text_value() == "B1+B3" &&
            column_one[1].reference.row == 3 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "row2-gap-a2",
        "public stationary formula insert_rows should expose rewritten formula in column snapshot");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "public stationary formula insert_rows save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "public stationary formula insert_rows save should persist projected bounds");
    check_contains(worksheet_xml,
        R"(<c r="A1"><f>B1+B3</f></c>)",
        "public stationary formula insert_rows save should persist rewritten formula");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "public stationary formula insert_rows save should persist source number");
    check_contains(worksheet_xml,
        R"(<c r="B3" t="inlineStr"><is><t>ref-b2</t></is></c>)",
        "public stationary formula insert_rows save should persist shifted source text");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "public stationary formula insert_rows save should omit inserted A2 gap");
    check_not_contains(worksheet_xml, R"(r="B2")",
        "public stationary formula insert_rows save should omit inserted B2 gap");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "public stationary formula insert_rows save should preserve untouched worksheets");
    check(!sheet.has_pending_changes(),
        "public stationary formula insert_rows save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "public stationary formula insert_rows save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "public stationary formula insert_rows save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "public stationary formula insert_rows save should clear dirty materialized memory");
}

void test_public_worksheet_editor_insert_shifts_preserve_absolute_formula_markers()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source_with_absolute_stationary_formula(
                "fastxlsx-workbook-editor-materialized-absolute-row-formula-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-materialized-absolute-row-formula-output.xlsx");
        const std::map<std::string, std::string> source_entries =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.insert_rows(2, 1);
        const std::size_t projected_cell_count = sheet.cell_count();
        const std::size_t projected_memory = sheet.estimated_memory_usage();

        check(!editor.last_edit_error().has_value(),
            "public absolute formula insert_rows should leave diagnostics clear");
        check(sheet.has_pending_changes(),
            "public absolute formula insert_rows should dirty the borrowed handle");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            "public absolute formula insert_rows should keep dirty materialized names");
        check(editor.pending_materialized_cell_count() == projected_cell_count,
            "public absolute formula insert_rows should report projected materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
            "public absolute formula insert_rows should report projected memory");
        check(projected_cell_count == 4,
            "public absolute formula insert_rows should preserve sparse record count");

        const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
        check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
                stationary_formula.text_value() == "B$3+$B3+$B$3+B1",
            "public absolute formula insert_rows should preserve absolute markers while moving rows");
        check(sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A3").text_value() == "row2-gap-a2" &&
                sheet.get_cell("B3").text_value() == "absolute-ref-b2",
            "public absolute formula insert_rows should shift source cells");
        check(!sheet.try_cell("A2").has_value() && !sheet.try_cell("B2").has_value(),
            "public absolute formula insert_rows should keep inserted row gaps empty");

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                row_one[0].value.text_value() == "B$3+$B3+$B$3+B1" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 2 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            "public absolute formula insert_rows should expose rewritten formula in row snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_two = sheet.column_cells(2);
        check(column_two.size() == 2 &&
                column_two[0].reference.row == 1 &&
                column_two[0].reference.column == 2 &&
                column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_two[0].value.number_value() == 1.0 &&
                column_two[1].reference.row == 3 &&
                column_two[1].reference.column == 2 &&
                column_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_two[1].value.text_value() == "absolute-ref-b2",
            "public absolute formula insert_rows should expose shifted reference column snapshot");

        editor.save_as(output);
        const std::map<std::string, std::string> output_entries =
            fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "public absolute formula insert_rows save should not mutate the source package");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
            "public absolute formula insert_rows save should persist projected bounds");
        check_contains(worksheet_xml,
            R"(<c r="A1"><f>B$3+$B3+$B$3+B1</f></c>)",
            "public absolute formula insert_rows save should persist rewritten formula");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "public absolute formula insert_rows save should persist source number");
        check_contains(worksheet_xml,
            R"(<c r="B3" t="inlineStr"><is><t>absolute-ref-b2</t></is></c>)",
            "public absolute formula insert_rows save should persist shifted source text");
        check_not_contains(worksheet_xml, R"(r="A2")",
            "public absolute formula insert_rows save should omit inserted A2 gap");
        check_not_contains(worksheet_xml, R"(r="B2")",
            "public absolute formula insert_rows save should omit inserted B2 gap");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "public absolute formula insert_rows save should preserve untouched worksheets");
        check(!sheet.has_pending_changes(),
            "public absolute formula insert_rows save should clean the borrowed handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "public absolute formula insert_rows save should clear dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "public absolute formula insert_rows save should clear dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "public absolute formula insert_rows save should clear dirty materialized memory");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source_with_absolute_stationary_formula(
                "fastxlsx-workbook-editor-materialized-absolute-column-formula-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-materialized-absolute-column-formula-output.xlsx");
        const std::map<std::string, std::string> source_entries =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.insert_columns(2, 1);
        const std::size_t projected_cell_count = sheet.cell_count();
        const std::size_t projected_memory = sheet.estimated_memory_usage();

        check(!editor.last_edit_error().has_value(),
            "public absolute formula insert_columns should leave diagnostics clear");
        check(sheet.has_pending_changes(),
            "public absolute formula insert_columns should dirty the borrowed handle");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            "public absolute formula insert_columns should keep dirty materialized names");
        check(editor.pending_materialized_cell_count() == projected_cell_count,
            "public absolute formula insert_columns should report projected materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
            "public absolute formula insert_columns should report projected memory");
        check(projected_cell_count == 4,
            "public absolute formula insert_columns should preserve sparse record count");

        const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
        check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
                stationary_formula.text_value() == "C$2+$C2+$C$2+C1",
            "public absolute formula insert_columns should preserve absolute markers while moving columns");
        check(sheet.get_cell("C1").number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "row2-gap-a2" &&
                sheet.get_cell("C2").text_value() == "absolute-ref-b2",
            "public absolute formula insert_columns should shift source cells");
        check(!sheet.try_cell("B1").has_value() && !sheet.try_cell("B2").has_value(),
            "public absolute formula insert_columns should keep inserted column gaps empty");

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                row_one[0].value.text_value() == "C$2+$C2+$C$2+C1" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 3 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            "public absolute formula insert_columns should expose rewritten formula in row snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_three = sheet.column_cells(3);
        check(column_three.size() == 2 &&
                column_three[0].reference.row == 1 &&
                column_three[0].reference.column == 3 &&
                column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_three[0].value.number_value() == 1.0 &&
                column_three[1].reference.row == 2 &&
                column_three[1].reference.column == 3 &&
                column_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_three[1].value.text_value() == "absolute-ref-b2",
            "public absolute formula insert_columns should expose shifted reference column snapshot");

        editor.save_as(output);
        const std::map<std::string, std::string> output_entries =
            fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "public absolute formula insert_columns save should not mutate the source package");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
            "public absolute formula insert_columns save should persist projected bounds");
        check_contains(worksheet_xml,
            R"(<c r="A1"><f>C$2+$C2+$C$2+C1</f></c>)",
            "public absolute formula insert_columns save should persist rewritten formula");
        check_contains(worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
            "public absolute formula insert_columns save should persist shifted source number");
        check_contains(worksheet_xml,
            R"(<c r="C2" t="inlineStr"><is><t>absolute-ref-b2</t></is></c>)",
            "public absolute formula insert_columns save should persist shifted source text");
        check_not_contains(worksheet_xml, R"(r="B1")",
            "public absolute formula insert_columns save should omit inserted B1 gap");
        check_not_contains(worksheet_xml, R"(r="B2")",
            "public absolute formula insert_columns save should omit inserted B2 gap");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "public absolute formula insert_columns save should preserve untouched worksheets");
        check(!sheet.has_pending_changes(),
            "public absolute formula insert_columns save should clean the borrowed handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "public absolute formula insert_columns save should clear dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "public absolute formula insert_columns save should clear dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "public absolute formula insert_columns save should clear dirty materialized memory");
    }
}

void test_public_worksheet_editor_insert_shifts_rewrite_stationary_range_formulas()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source_with_stationary_range_formula(
                "fastxlsx-workbook-editor-materialized-range-row-formula-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-materialized-range-row-formula-output.xlsx");
        const std::map<std::string, std::string> source_entries =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.insert_rows(2, 1);
        const std::size_t projected_cell_count = sheet.cell_count();
        const std::size_t projected_memory = sheet.estimated_memory_usage();

        check(!editor.last_edit_error().has_value(),
            "public range formula insert_rows should leave diagnostics clear");
        check(sheet.has_pending_changes(),
            "public range formula insert_rows should dirty the borrowed handle");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            "public range formula insert_rows should keep dirty materialized names");
        check(editor.pending_materialized_cell_count() == projected_cell_count,
            "public range formula insert_rows should report projected materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
            "public range formula insert_rows should report projected memory");
        check(projected_cell_count == 4,
            "public range formula insert_rows should preserve sparse record count");

        const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
        check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
                stationary_formula.text_value() == "SUM(B1:B3)+B:B+3:3",
            "public range formula insert_rows should rewrite range and whole-row references");
        check(sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A3").text_value() == "row2-gap-a2" &&
                sheet.get_cell("B3").text_value() == "range-ref-b2",
            "public range formula insert_rows should shift source cells");
        check(!sheet.try_cell("A2").has_value() && !sheet.try_cell("B2").has_value(),
            "public range formula insert_rows should keep inserted row gaps empty");

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                row_one[0].value.text_value() == "SUM(B1:B3)+B:B+3:3" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 2 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            "public range formula insert_rows should expose rewritten formula in row snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_two = sheet.column_cells(2);
        check(column_two.size() == 2 &&
                column_two[0].reference.row == 1 &&
                column_two[0].reference.column == 2 &&
                column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_two[0].value.number_value() == 1.0 &&
                column_two[1].reference.row == 3 &&
                column_two[1].reference.column == 2 &&
                column_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_two[1].value.text_value() == "range-ref-b2",
            "public range formula insert_rows should expose shifted source column snapshot");

        editor.save_as(output);
        const std::map<std::string, std::string> output_entries =
            fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "public range formula insert_rows save should not mutate the source package");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
            "public range formula insert_rows save should persist projected bounds");
        check_contains(worksheet_xml,
            R"(<c r="A1"><f>SUM(B1:B3)+B:B+3:3</f></c>)",
            "public range formula insert_rows save should persist rewritten formula");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "public range formula insert_rows save should persist source number");
        check_contains(worksheet_xml,
            R"(<c r="B3" t="inlineStr"><is><t>range-ref-b2</t></is></c>)",
            "public range formula insert_rows save should persist shifted source text");
        check_not_contains(worksheet_xml, R"(r="A2")",
            "public range formula insert_rows save should omit inserted A2 gap");
        check_not_contains(worksheet_xml, R"(r="B2")",
            "public range formula insert_rows save should omit inserted B2 gap");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "public range formula insert_rows save should preserve untouched worksheets");
        check(!sheet.has_pending_changes(),
            "public range formula insert_rows save should clean the borrowed handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "public range formula insert_rows save should clear dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "public range formula insert_rows save should clear dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "public range formula insert_rows save should clear dirty materialized memory");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source_with_stationary_range_formula(
                "fastxlsx-workbook-editor-materialized-range-column-formula-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-materialized-range-column-formula-output.xlsx");
        const std::map<std::string, std::string> source_entries =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.insert_columns(2, 1);
        const std::size_t projected_cell_count = sheet.cell_count();
        const std::size_t projected_memory = sheet.estimated_memory_usage();

        check(!editor.last_edit_error().has_value(),
            "public range formula insert_columns should leave diagnostics clear");
        check(sheet.has_pending_changes(),
            "public range formula insert_columns should dirty the borrowed handle");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            "public range formula insert_columns should keep dirty materialized names");
        check(editor.pending_materialized_cell_count() == projected_cell_count,
            "public range formula insert_columns should report projected materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
            "public range formula insert_columns should report projected memory");
        check(projected_cell_count == 4,
            "public range formula insert_columns should preserve sparse record count");

        const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
        check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
                stationary_formula.text_value() == "SUM(C1:C2)+C:C+2:2",
            "public range formula insert_columns should rewrite range and whole-column references");
        check(sheet.get_cell("C1").number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "row2-gap-a2" &&
                sheet.get_cell("C2").text_value() == "range-ref-b2",
            "public range formula insert_columns should shift source cells");
        check(!sheet.try_cell("B1").has_value() && !sheet.try_cell("B2").has_value(),
            "public range formula insert_columns should keep inserted column gaps empty");

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                row_one[0].value.text_value() == "SUM(C1:C2)+C:C+2:2" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 3 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            "public range formula insert_columns should expose rewritten formula in row snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_three = sheet.column_cells(3);
        check(column_three.size() == 2 &&
                column_three[0].reference.row == 1 &&
                column_three[0].reference.column == 3 &&
                column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_three[0].value.number_value() == 1.0 &&
                column_three[1].reference.row == 2 &&
                column_three[1].reference.column == 3 &&
                column_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_three[1].value.text_value() == "range-ref-b2",
            "public range formula insert_columns should expose shifted source column snapshot");

        editor.save_as(output);
        const std::map<std::string, std::string> output_entries =
            fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "public range formula insert_columns save should not mutate the source package");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
            "public range formula insert_columns save should persist projected bounds");
        check_contains(worksheet_xml,
            R"(<c r="A1"><f>SUM(C1:C2)+C:C+2:2</f></c>)",
            "public range formula insert_columns save should persist rewritten formula");
        check_contains(worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
            "public range formula insert_columns save should persist shifted source number");
        check_contains(worksheet_xml,
            R"(<c r="C2" t="inlineStr"><is><t>range-ref-b2</t></is></c>)",
            "public range formula insert_columns save should persist shifted source text");
        check_not_contains(worksheet_xml, R"(r="B1")",
            "public range formula insert_columns save should omit inserted B1 gap");
        check_not_contains(worksheet_xml, R"(r="B2")",
            "public range formula insert_columns save should omit inserted B2 gap");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "public range formula insert_columns save should preserve untouched worksheets");
        check(!sheet.has_pending_changes(),
            "public range formula insert_columns save should clean the borrowed handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "public range formula insert_columns save should clear dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "public range formula insert_columns save should clear dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "public range formula insert_columns save should clear dirty materialized memory");
    }
}

void test_public_worksheet_editor_delete_shifts_rewrite_stationary_range_formulas()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source_with_stationary_range_row_delete_formula(
                "fastxlsx-workbook-editor-materialized-range-delete-row-formula-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-materialized-range-delete-row-formula-output.xlsx");
        const std::map<std::string, std::string> source_entries =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.delete_rows(2, 1);
        const std::size_t projected_cell_count = sheet.cell_count();
        const std::size_t projected_memory = sheet.estimated_memory_usage();

        check(!editor.last_edit_error().has_value(),
            "public range formula delete_rows should leave diagnostics clear");
        check(sheet.has_pending_changes(),
            "public range formula delete_rows should dirty the borrowed handle");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            "public range formula delete_rows should keep dirty materialized names");
        check(editor.pending_materialized_cell_count() == projected_cell_count,
            "public range formula delete_rows should report projected materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
            "public range formula delete_rows should report projected memory");
        check(projected_cell_count == 4,
            "public range formula delete_rows should remove deleted sparse records");

        const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
        check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
                stationary_formula.text_value() == "SUM(#REF!:B2)+B:B+#REF!",
            "public range formula delete_rows should rewrite deleted range endpoints");
        check(sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "row3-gap-a3" &&
                sheet.get_cell("B2").text_value() == "range-ref-b3",
            "public range formula delete_rows should shift source cells");
        check(!sheet.try_cell("A3").has_value() && !sheet.try_cell("B3").has_value(),
            "public range formula delete_rows should remove old row coordinates");

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                row_one[0].value.text_value() == "SUM(#REF!:B2)+B:B+#REF!" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 2 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            "public range formula delete_rows should expose rewritten formula in row snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_two = sheet.column_cells(2);
        check(column_two.size() == 2 &&
                column_two[0].reference.row == 1 &&
                column_two[0].reference.column == 2 &&
                column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_two[0].value.number_value() == 1.0 &&
                column_two[1].reference.row == 2 &&
                column_two[1].reference.column == 2 &&
                column_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_two[1].value.text_value() == "range-ref-b3",
            "public range formula delete_rows should expose shifted source column snapshot");

        editor.save_as(output);
        const std::map<std::string, std::string> output_entries =
            fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "public range formula delete_rows save should not mutate the source package");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "public range formula delete_rows save should persist projected bounds");
        check_contains(worksheet_xml,
            R"(<c r="A1"><f>SUM(#REF!:B2)+B:B+#REF!</f></c>)",
            "public range formula delete_rows save should persist rewritten formula");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "public range formula delete_rows save should persist source number");
        check_contains(worksheet_xml,
            R"(<c r="B2" t="inlineStr"><is><t>range-ref-b3</t></is></c>)",
            "public range formula delete_rows save should persist shifted source text");
        check_not_contains(worksheet_xml, "delete-a2",
            "public range formula delete_rows save should omit deleted row text");
        check_not_contains(worksheet_xml, "delete-b2",
            "public range formula delete_rows save should omit deleted referenced text");
        check_not_contains(worksheet_xml, R"(r="A3")",
            "public range formula delete_rows save should omit old A3 coordinate");
        check_not_contains(worksheet_xml, R"(r="B3")",
            "public range formula delete_rows save should omit old B3 coordinate");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "public range formula delete_rows save should preserve untouched worksheets");
        check(!sheet.has_pending_changes(),
            "public range formula delete_rows save should clean the borrowed handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "public range formula delete_rows save should clear dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "public range formula delete_rows save should clear dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "public range formula delete_rows save should clear dirty materialized memory");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source_with_stationary_range_column_delete_formula(
                "fastxlsx-workbook-editor-materialized-range-delete-column-formula-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-materialized-range-delete-column-formula-output.xlsx");
        const std::map<std::string, std::string> source_entries =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.delete_columns(2, 1);
        const std::size_t projected_cell_count = sheet.cell_count();
        const std::size_t projected_memory = sheet.estimated_memory_usage();

        check(!editor.last_edit_error().has_value(),
            "public range formula delete_columns should leave diagnostics clear");
        check(sheet.has_pending_changes(),
            "public range formula delete_columns should dirty the borrowed handle");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            "public range formula delete_columns should keep dirty materialized names");
        check(editor.pending_materialized_cell_count() == projected_cell_count,
            "public range formula delete_columns should report projected materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
            "public range formula delete_columns should report projected memory");
        check(projected_cell_count == 4,
            "public range formula delete_columns should remove deleted sparse records");

        const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
        check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
                stationary_formula.text_value() == "SUM(#REF!:B1)+#REF!+1:1",
            "public range formula delete_columns should rewrite deleted range endpoints");
        check(sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "row2-gap-a2" &&
                sheet.get_cell("B2").text_value() == "range-ref-c2",
            "public range formula delete_columns should shift source cells");
        check(!sheet.try_cell("C1").has_value() && !sheet.try_cell("C2").has_value(),
            "public range formula delete_columns should remove old column coordinates");

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                row_one[0].value.text_value() == "SUM(#REF!:B1)+#REF!+1:1" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 2 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            "public range formula delete_columns should expose rewritten formula in row snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_two = sheet.column_cells(2);
        check(column_two.size() == 2 &&
                column_two[0].reference.row == 1 &&
                column_two[0].reference.column == 2 &&
                column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_two[0].value.number_value() == 1.0 &&
                column_two[1].reference.row == 2 &&
                column_two[1].reference.column == 2 &&
                column_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_two[1].value.text_value() == "range-ref-c2",
            "public range formula delete_columns should expose shifted source column snapshot");

        editor.save_as(output);
        const std::map<std::string, std::string> output_entries =
            fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "public range formula delete_columns save should not mutate the source package");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "public range formula delete_columns save should persist projected bounds");
        check_contains(worksheet_xml,
            R"(<c r="A1"><f>SUM(#REF!:B1)+#REF!+1:1</f></c>)",
            "public range formula delete_columns save should persist rewritten formula");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "public range formula delete_columns save should persist shifted source number");
        check_contains(worksheet_xml,
            R"(<c r="B2" t="inlineStr"><is><t>range-ref-c2</t></is></c>)",
            "public range formula delete_columns save should persist shifted source text");
        check_not_contains(worksheet_xml, "delete-b1",
            "public range formula delete_columns save should omit deleted row-one text");
        check_not_contains(worksheet_xml, "delete-b2",
            "public range formula delete_columns save should omit deleted row-two text");
        check_not_contains(worksheet_xml, R"(r="C1")",
            "public range formula delete_columns save should omit old C1 coordinate");
        check_not_contains(worksheet_xml, R"(r="C2")",
            "public range formula delete_columns save should omit old C2 coordinate");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "public range formula delete_columns save should preserve untouched worksheets");
        check(!sheet.has_pending_changes(),
            "public range formula delete_columns save should clean the borrowed handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "public range formula delete_columns save should clear dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "public range formula delete_columns save should clear dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "public range formula delete_columns save should clear dirty materialized memory");
    }
}

void test_public_worksheet_editor_delete_shifts_preserve_absolute_formula_markers()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source_with_absolute_row_delete_formula(
                "fastxlsx-workbook-editor-materialized-absolute-delete-row-formula-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-materialized-absolute-delete-row-formula-output.xlsx");
        const std::map<std::string, std::string> source_entries =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.delete_rows(2, 1);
        const std::size_t projected_cell_count = sheet.cell_count();
        const std::size_t projected_memory = sheet.estimated_memory_usage();

        check(!editor.last_edit_error().has_value(),
            "public absolute formula delete_rows should leave diagnostics clear");
        check(sheet.has_pending_changes(),
            "public absolute formula delete_rows should dirty the borrowed handle");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            "public absolute formula delete_rows should keep dirty materialized names");
        check(editor.pending_materialized_cell_count() == projected_cell_count,
            "public absolute formula delete_rows should report projected materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
            "public absolute formula delete_rows should report projected memory");
        check(projected_cell_count == 4,
            "public absolute formula delete_rows should remove deleted sparse records");

        const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
        check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
                stationary_formula.text_value() == "#REF!+$B2+$B$2+B1",
            "public absolute formula delete_rows should preserve absolute markers on surviving references");
        check(sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "row3-gap-a3" &&
                sheet.get_cell("B2").text_value() == "absolute-ref-b3",
            "public absolute formula delete_rows should shift source cells");
        check(!sheet.try_cell("A3").has_value() && !sheet.try_cell("B3").has_value(),
            "public absolute formula delete_rows should remove old row coordinates");

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                row_one[0].value.text_value() == "#REF!+$B2+$B$2+B1" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 2 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            "public absolute formula delete_rows should expose rewritten formula in row snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_two = sheet.column_cells(2);
        check(column_two.size() == 2 &&
                column_two[0].reference.row == 1 &&
                column_two[0].reference.column == 2 &&
                column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_two[0].value.number_value() == 1.0 &&
                column_two[1].reference.row == 2 &&
                column_two[1].reference.column == 2 &&
                column_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_two[1].value.text_value() == "absolute-ref-b3",
            "public absolute formula delete_rows should expose shifted reference column snapshot");

        editor.save_as(output);
        const std::map<std::string, std::string> output_entries =
            fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "public absolute formula delete_rows save should not mutate the source package");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "public absolute formula delete_rows save should persist projected bounds");
        check_contains(worksheet_xml,
            R"(<c r="A1"><f>#REF!+$B2+$B$2+B1</f></c>)",
            "public absolute formula delete_rows save should persist rewritten formula");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "public absolute formula delete_rows save should persist source number");
        check_contains(worksheet_xml,
            R"(<c r="B2" t="inlineStr"><is><t>absolute-ref-b3</t></is></c>)",
            "public absolute formula delete_rows save should persist shifted source text");
        check_not_contains(worksheet_xml, "delete-a2",
            "public absolute formula delete_rows save should omit deleted row text");
        check_not_contains(worksheet_xml, "delete-b2",
            "public absolute formula delete_rows save should omit deleted referenced text");
        check_not_contains(worksheet_xml, R"(r="A3")",
            "public absolute formula delete_rows save should omit old A3 coordinate");
        check_not_contains(worksheet_xml, R"(r="B3")",
            "public absolute formula delete_rows save should omit old B3 coordinate");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "public absolute formula delete_rows save should preserve untouched worksheets");
        check(!sheet.has_pending_changes(),
            "public absolute formula delete_rows save should clean the borrowed handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "public absolute formula delete_rows save should clear dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "public absolute formula delete_rows save should clear dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "public absolute formula delete_rows save should clear dirty materialized memory");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source_with_absolute_column_delete_formula(
                "fastxlsx-workbook-editor-materialized-absolute-delete-column-formula-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-materialized-absolute-delete-column-formula-output.xlsx");
        const std::map<std::string, std::string> source_entries =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.delete_columns(2, 1);
        const std::size_t projected_cell_count = sheet.cell_count();
        const std::size_t projected_memory = sheet.estimated_memory_usage();

        check(!editor.last_edit_error().has_value(),
            "public absolute formula delete_columns should leave diagnostics clear");
        check(sheet.has_pending_changes(),
            "public absolute formula delete_columns should dirty the borrowed handle");
        check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
            "public absolute formula delete_columns should keep dirty materialized names");
        check(editor.pending_materialized_cell_count() == projected_cell_count,
            "public absolute formula delete_columns should report projected materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
            "public absolute formula delete_columns should report projected memory");
        check(projected_cell_count == 4,
            "public absolute formula delete_columns should remove deleted sparse records");

        const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
        check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
                stationary_formula.text_value() == "#REF!+$B2+$B$2+A1",
            "public absolute formula delete_columns should preserve absolute markers on surviving references");
        check(sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "row2-gap-a2" &&
                sheet.get_cell("B2").text_value() == "absolute-ref-c2",
            "public absolute formula delete_columns should shift source cells");
        check(!sheet.try_cell("C1").has_value() && !sheet.try_cell("C2").has_value(),
            "public absolute formula delete_columns should remove old column coordinates");

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                row_one[0].value.text_value() == "#REF!+$B2+$B$2+A1" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 2 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            "public absolute formula delete_columns should expose rewritten formula in row snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_two = sheet.column_cells(2);
        check(column_two.size() == 2 &&
                column_two[0].reference.row == 1 &&
                column_two[0].reference.column == 2 &&
                column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_two[0].value.number_value() == 1.0 &&
                column_two[1].reference.row == 2 &&
                column_two[1].reference.column == 2 &&
                column_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_two[1].value.text_value() == "absolute-ref-c2",
            "public absolute formula delete_columns should expose shifted reference column snapshot");

        editor.save_as(output);
        const std::map<std::string, std::string> output_entries =
            fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "public absolute formula delete_columns save should not mutate the source package");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "public absolute formula delete_columns save should persist projected bounds");
        check_contains(worksheet_xml,
            R"(<c r="A1"><f>#REF!+$B2+$B$2+A1</f></c>)",
            "public absolute formula delete_columns save should persist rewritten formula");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "public absolute formula delete_columns save should persist shifted source number");
        check_contains(worksheet_xml,
            R"(<c r="B2" t="inlineStr"><is><t>absolute-ref-c2</t></is></c>)",
            "public absolute formula delete_columns save should persist shifted source text");
        check_not_contains(worksheet_xml, "delete-b1",
            "public absolute formula delete_columns save should omit deleted row-one text");
        check_not_contains(worksheet_xml, "delete-b2",
            "public absolute formula delete_columns save should omit deleted row-two text");
        check_not_contains(worksheet_xml, R"(r="C1")",
            "public absolute formula delete_columns save should omit old C1 coordinate");
        check_not_contains(worksheet_xml, R"(r="C2")",
            "public absolute formula delete_columns save should omit old C2 coordinate");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "public absolute formula delete_columns save should preserve untouched worksheets");
        check(!sheet.has_pending_changes(),
            "public absolute formula delete_columns save should clean the borrowed handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "public absolute formula delete_columns save should clear dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "public absolute formula delete_columns save should clear dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "public absolute formula delete_columns save should clear dirty materialized memory");
    }
}

void test_public_worksheet_editor_delete_rows_rewrites_stationary_formula()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_row_delete_formula(
            "fastxlsx-workbook-editor-materialized-stationary-delete-row-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-stationary-delete-row-formula-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_rows(2, 1);
    const std::size_t projected_cell_count = sheet.cell_count();
    const std::size_t projected_memory = sheet.estimated_memory_usage();

    check(!editor.last_edit_error().has_value(),
        "public stationary formula delete_rows should leave diagnostics clear");
    check(sheet.has_pending_changes(),
        "public stationary formula delete_rows should dirty the borrowed handle");
    check(editor.pending_change_count() == 0,
        "public stationary formula delete_rows should not queue coarse edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "public stationary formula delete_rows should keep dirty materialized names");
    check(editor.pending_materialized_cell_count() == projected_cell_count,
        "public stationary formula delete_rows should report projected materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
        "public stationary formula delete_rows should report projected memory");
    check(projected_cell_count == 4,
        "public stationary formula delete_rows should remove deleted sparse records");

    const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
    check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
            stationary_formula.text_value() == "#REF!+B2",
        "public stationary formula delete_rows should rewrite deleted references");
    check(sheet.get_cell("B1").number_value() == 1.0 &&
            sheet.get_cell("A2").text_value() == "row3-gap-a3" &&
            sheet.get_cell("B2").text_value() == "ref-b3",
        "public stationary formula delete_rows should shift referenced source cells");
    check(!sheet.try_cell("A3").has_value() && !sheet.try_cell("B3").has_value(),
        "public stationary formula delete_rows should remove old row coordinates");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_one[0].value.text_value() == "#REF!+B2" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        "public stationary formula delete_rows should expose rewritten formula in row snapshot");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one = sheet.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            column_one[0].value.text_value() == "#REF!+B2" &&
            column_one[1].reference.row == 2 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "row3-gap-a3",
        "public stationary formula delete_rows should expose rewritten formula in column snapshot");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "public stationary formula delete_rows save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "public stationary formula delete_rows save should persist projected bounds");
    check_contains(worksheet_xml,
        R"(<c r="A1"><f>#REF!+B2</f></c>)",
        "public stationary formula delete_rows save should persist rewritten formula");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "public stationary formula delete_rows save should persist source number");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>ref-b3</t></is></c>)",
        "public stationary formula delete_rows save should persist shifted source text");
    check_not_contains(worksheet_xml, "delete-a2",
        "public stationary formula delete_rows save should omit deleted row text");
    check_not_contains(worksheet_xml, "delete-b2",
        "public stationary formula delete_rows save should omit deleted referenced text");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "public stationary formula delete_rows save should omit old A3 coordinate");
    check_not_contains(worksheet_xml, R"(r="B3")",
        "public stationary formula delete_rows save should omit old B3 coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "public stationary formula delete_rows save should preserve untouched worksheets");
    check(!sheet.has_pending_changes(),
        "public stationary formula delete_rows save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "public stationary formula delete_rows save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "public stationary formula delete_rows save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "public stationary formula delete_rows save should clear dirty materialized memory");
}

void test_public_worksheet_editor_delete_columns_rewrites_stationary_formula()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_delete_formula(
            "fastxlsx-workbook-editor-materialized-stationary-delete-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-stationary-delete-formula-output.xlsx");
    const std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_columns(2, 1);
    const std::size_t projected_cell_count = sheet.cell_count();
    const std::size_t projected_memory = sheet.estimated_memory_usage();

    check(!editor.last_edit_error().has_value(),
        "public stationary formula delete_columns should leave diagnostics clear");
    check(sheet.has_pending_changes(),
        "public stationary formula delete_columns should dirty the borrowed handle");
    check(editor.pending_change_count() == 0,
        "public stationary formula delete_columns should not queue coarse edits before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "public stationary formula delete_columns should keep dirty materialized names");
    check(editor.pending_materialized_cell_count() == projected_cell_count,
        "public stationary formula delete_columns should report projected materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == projected_memory,
        "public stationary formula delete_columns should report projected memory");
    check(projected_cell_count == 4,
        "public stationary formula delete_columns should remove deleted sparse records");

    const fastxlsx::CellValue stationary_formula = sheet.get_cell("A1");
    check(stationary_formula.kind() == fastxlsx::CellValueKind::Formula &&
            stationary_formula.text_value() == "B1+B2",
        "public stationary formula delete_columns should rewrite affected references");
    check(sheet.get_cell("B1").number_value() == 1.0 &&
            sheet.get_cell("A2").text_value() == "row2-gap-a2" &&
            sheet.get_cell("B2").text_value() == "ref-c2",
        "public stationary formula delete_columns should shift referenced source cells");
    check(!sheet.try_cell("C1").has_value() && !sheet.try_cell("C2").has_value(),
        "public stationary formula delete_columns should remove old column coordinates");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_one[0].value.text_value() == "B1+B2" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        "public stationary formula delete_columns should expose rewritten formula in row snapshot");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one = sheet.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            column_one[0].value.text_value() == "B1+B2" &&
            column_one[1].reference.row == 2 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "row2-gap-a2",
        "public stationary formula delete_columns should expose rewritten formula in column snapshot");

    editor.save_as(output);
    const std::map<std::string, std::string> output_entries =
        fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "public stationary formula delete_columns save should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "public stationary formula delete_columns save should persist projected bounds");
    check_contains(worksheet_xml,
        R"(<c r="A1"><f>B1+B2</f></c>)",
        "public stationary formula delete_columns save should persist rewritten formula");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "public stationary formula delete_columns save should persist shifted source number");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>ref-c2</t></is></c>)",
        "public stationary formula delete_columns save should persist shifted source text");
    check_not_contains(worksheet_xml, "delete-b1",
        "public stationary formula delete_columns save should omit deleted row-one text");
    check_not_contains(worksheet_xml, "delete-b2",
        "public stationary formula delete_columns save should omit deleted row-two text");
    check_not_contains(worksheet_xml, R"(r="C1")",
        "public stationary formula delete_columns save should omit old C1 coordinate");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "public stationary formula delete_columns save should omit old C2 coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "public stationary formula delete_columns save should preserve untouched worksheets");
    check(!sheet.has_pending_changes(),
        "public stationary formula delete_columns save should clean the borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "public stationary formula delete_columns save should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "public stationary formula delete_columns save should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "public stationary formula delete_columns save should clear dirty materialized memory");
}

void test_internal_materialized_session_assignment_from_moved_from_source_clears_target()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-assign-moved-from-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-assign-moved-from-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-assign-moved-from-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1,
        fastxlsx::CellValue::text("held-materialized-state"));

    fastxlsx::WorkbookEditor holder = std::move(editor);
    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "materialized source editor should be moved-from before assignment");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        target, "Untouched", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Untouched", 1, 1,
        fastxlsx::CellValue::text("target-materialized-should-clear"));
    target.replace_sheet_data(
        "Data", {{fastxlsx::CellValue::text("target-public-should-clear")}});
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(target) == 1,
        "target should start with one materialized session before moved-from assignment");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 1,
        "target should start with dirty materialized state before moved-from assignment");
    check(target.pending_change_count() == 1,
        "target should start with public queued state before moved-from assignment");

    target = std::move(editor);

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(target) == 0,
        "assignment from moved-from source should clear target materialized sessions");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "assignment from moved-from source should clear target dirty materialized state");
    check(!target.has_pending_changes(),
        "assignment from moved-from source should clear target public pending changes");
    check(target.pending_change_count() == 0,
        "assignment from moved-from source should clear target public pending count");
    check(target.pending_replacement_cell_count() == 0,
        "assignment from moved-from source should clear target replacement diagnostics");
    check(target.pending_replacement_worksheet_names().empty(),
        "assignment from moved-from source should clear target replacement names");
    check(!target.last_edit_error().has_value(),
        "assignment from moved-from source should clear target last_edit_error");
    check(threw_fastxlsx_error([&] { target.save_as(output); }),
        "assignment from moved-from source should leave target unable to save stale state");

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(holder) == 1,
        "assignment from moved-from source should not disturb prior moved-to holder");
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(holder);
    holder.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "held-materialized-state",
        "prior moved-to holder should still save its materialized payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "target-public-should-clear",
        "prior moved-to holder should not receive cleared target public payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet2.xml"),
        "target-materialized-should-clear",
        "prior moved-to holder should not receive cleared target materialized payload");
}

void test_internal_materialized_session_blocks_whole_sheet_replacement()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-mixing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-mixing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("materialized-dirty"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("blocked")}});
    }), "replace_sheet_data should be rejected after materializing the same planned sheet");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Data"),
        "blocked materialized replacement should preserve the private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "blocked materialized replacement should preserve private dirty state");
    check(editor.has_pending_changes(),
        "dirty materialized replacement state should make save_as pending");
    check(editor.pending_replacement_cell_count() == 0,
        "blocked materialized replacement should not record replacement cells");
    check(editor.pending_replacement_worksheet_names().empty(),
        "blocked materialized replacement should not add pending replacement names");
    check(editor.last_edit_error().has_value(),
        "blocked materialized replacement should record a public edit diagnostic");

    editor.replace_sheet_data("Untouched", {{fastxlsx::CellValue::text("allowed-other-sheet")}});
    check(editor.has_pending_changes(),
        "materializing one sheet should not block replacement of a different sheet");
    check(editor.pending_replacement_cell_count() == 1,
        "allowed replacement on a different sheet should record its payload");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "blocked",
        "blocked materialized replacement should not leak into the source sheet output");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "materialized-dirty",
        "dirty materialized state should auto-flush on save_as");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "allowed-other-sheet",
        "replacement on a different sheet should still be saved");
}

void test_internal_materialized_session_blocks_materialize_after_public_replacement()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-after-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-after-replace-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("queued-replacement")}});

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "Data", "Data");
    }), "materializing a planned sheet with queued replacement data should be rejected");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "blocked materialize-after-replacement should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "blocked materialize-after-replacement should not create dirty private state");
    check(editor.pending_change_count() == 1,
        "blocked materialize-after-replacement should preserve queued public edit count");
    check(editor.pending_replacement_cell_count() == 1,
        "blocked materialize-after-replacement should preserve replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "test-hook materialize-after-replacement failure should not update public last_edit_error");

    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Untouched", "Untouched");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Untouched"),
        "queued replacement on one sheet should not block materializing another sheet");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "queued-replacement",
        "blocked materialize-after-replacement should preserve staged replacement output");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "blocked materialize-after-replacement should not restore source sheet data");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "clean materialization of another sheet should not change untouched output");
}

void test_internal_materialized_session_blocks_materialize_after_renamed_public_replacement()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-after-renamed-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-after-renamed-replace-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("queued-before-rename")}});
    editor.rename_sheet("Data", "QueuedData");

    check(editor.has_pending_replacement("QueuedData"),
        "rename after replacement should migrate replacement diagnostics to the planned name");
    check(!editor.has_pending_replacement("Data"),
        "rename after replacement should remove replacement diagnostics for the old name");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "QueuedData",
            "pending replacement name list should expose the renamed planned sheet");
    }

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "QueuedData", "Data");
    }), "materializing a renamed planned sheet with queued replacement data should be rejected");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "blocked renamed materialize-after-replacement should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "blocked renamed materialize-after-replacement should not create dirty private state");
    check(editor.pending_change_count() == 2,
        "blocked renamed materialize-after-replacement should preserve public edit count");
    check(editor.pending_replacement_cell_count() == 1,
        "blocked renamed materialize-after-replacement should preserve replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "test-hook renamed materialize-after-replacement failure should not update public last_edit_error");

    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Untouched", "Untouched");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Untouched"),
        "renamed queued replacement should not block materializing another sheet");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="QueuedData")",
        "renamed replacement output should keep the planned workbook sheet name");
    check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "renamed replacement output should not restore the source workbook sheet name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "queued-before-rename",
        "blocked renamed materialize-after-replacement should preserve staged replacement output");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "blocked renamed materialize-after-replacement should not restore source sheet data");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "clean materialization of another sheet should not change untouched output");
}

void test_internal_materialized_session_blocks_materialize_after_replacement_on_renamed_sheet()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-replace-on-renamed-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-replace-on-renamed-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "RenamedThenReplaced");
    editor.replace_sheet_data("RenamedThenReplaced",
        {{fastxlsx::CellValue::text("replace-after-rename")}});

    check(editor.has_pending_replacement("RenamedThenReplaced"),
        "replacement after rename should track the renamed planned sheet");
    check(!editor.has_pending_replacement("Data"),
        "replacement after rename should not track the old source sheet name");
    check(editor.pending_change_count() == 2,
        "rename plus replacement should queue two public facade edits");

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "RenamedThenReplaced", "Data");
    }), "materializing a renamed sheet after queued replacement should be rejected");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "blocked replacement-on-renamed materialize should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "blocked replacement-on-renamed materialize should not create dirty private state");
    check(editor.pending_change_count() == 2,
        "blocked replacement-on-renamed materialize should preserve public edit count");
    check(editor.pending_replacement_cell_count() == 1,
        "blocked replacement-on-renamed materialize should preserve replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "test-hook replacement-on-renamed materialize failure should not update public last_edit_error");

    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Untouched", "Untouched");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Untouched"),
        "replacement on renamed sheet should not block materializing another sheet");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedThenReplaced")",
        "replacement-on-renamed output should keep the planned workbook sheet name");
    check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "replacement-on-renamed output should not restore the source workbook sheet name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "replace-after-rename",
        "blocked replacement-on-renamed materialize should preserve staged replacement output");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "blocked replacement-on-renamed materialize should not restore source sheet data");
}

void test_internal_materialized_session_flushes_dirty_projection_to_patch_plan()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-flush-source.xlsx");
    const std::filesystem::path clean_output =
        artifact("fastxlsx-workbook-editor-materialized-flush-clean-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-materialized-flush-dirty-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(!editor.has_pending_changes(),
        "flushing clean materialized sessions should not queue public edit diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "flushing clean materialized sessions should leave no dirty sessions");

    editor.save_as(clean_output);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto clean_output_entries = fastxlsx::test::read_zip_entries(clean_output);
    check(clean_output_entries == source_entries,
        "clean materialization flush should keep no-op save_as as a source roundtrip");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("flushed-materialized"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "test hook set_cell should mark the materialized session dirty before flush");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.has_pending_changes(),
        "flushing dirty materialized sessions should queue coarse public edit diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "successful materialized flush should clear private dirty state");

    editor.save_as(dirty_output);
    const auto dirty_output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check_contains(dirty_output_entries.at("xl/worksheets/sheet1.xml"), "flushed-materialized",
        "dirty materialized flush should save the projected worksheet cell");
    check_contains(dirty_output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<dimension ref="A1:B2"/>)",
        "dirty materialized flush should save the refreshed sparse-store dimension");
    check(dirty_output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty materialized flush should preserve untouched worksheet bytes");
}

void test_internal_materialized_session_blocks_same_sheet_rename()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-rename-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-rename-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-before-rename"));

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedRename"); }),
        "rename_sheet should reject the same planned sheet after materialization");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Data"),
        "blocked materialized rename should preserve the private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "blocked materialized rename should preserve dirty private state");
    check(editor.has_pending_changes(),
        "dirty materialized rename state should make save_as pending");
    check(editor.last_edit_error().has_value(),
        "blocked materialized rename should record a public edit diagnostic");
    {
        const std::vector<std::string> names = editor.worksheet_names();
        check(names.size() == 2 && names[0] == "Data" && names[1] == "Untouched",
            "blocked materialized rename should preserve planned catalog names");
    }

    editor.rename_sheet("Untouched", "OtherRenamed");
    check(editor.has_pending_changes(),
        "materializing one sheet should not block renaming a different sheet");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "renaming another sheet should not clear dirty materialized state");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "blocked materialized rename output should keep the original materialized sheet name");
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="OtherRenamed")",
        "rename of a different sheet should still be saved");
    check_not_contains(output_entries.at("xl/workbook.xml"), "BlockedRename",
        "blocked materialized rename should not leak the rejected name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "dirty-before-rename",
        "dirty materialized cells should auto-flush through save_as");
}

void test_internal_materialized_session_flushes_after_rejected_public_operations()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-rejected-public-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-rejected-public-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-after-rejected-public-op"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data(
            "Data", {{fastxlsx::CellValue::text("blocked-public-replacement")}});
    }), "replace_sheet_data should reject a dirty materialized sheet before staging payload");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedRename"); }),
        "rename_sheet should reject a dirty materialized sheet before catalog mutation");

    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Data"),
        "rejected public operations should preserve the private materialized session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "rejected public operations should preserve dirty materialized state");
    check(editor.has_pending_changes(),
        "dirty materialized state should keep save_as pending after rejected public operations");
    check(editor.pending_replacement_cell_count() == 0,
        "rejected public replacement should not leave staged replacement cells");
    check(editor.pending_replacement_worksheet_names().empty(),
        "rejected public replacement should not leave staged replacement names");
    check(editor.last_edit_error().has_value(),
        "rejected public operations should record a public edit diagnostic");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 1,
        "explicit materialized flush after rejected public operations should queue one handoff");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "explicit materialized flush after rejected public operations should clear dirty state");
    check(editor.pending_replacement_cell_count() == 0,
        "materialized flush should not reuse rejected sheetData replacement diagnostics");
    check(editor.pending_replacement_worksheet_names().empty(),
        "materialized flush should not expose rejected replacement sheet names");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "flush after rejected public operations should preserve the original sheet name");
    check_not_contains(workbook_xml, "BlockedRename",
        "flush after rejected public operations should not leak the rejected sheet name");
    check_contains(worksheet_xml, "dirty-after-rejected-public-op",
        "flush after rejected public operations should save the dirty materialized payload");
    check_not_contains(worksheet_xml, "blocked-public-replacement",
        "flush after rejected public operations should not leak the rejected replacement payload");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "flush after rejected public operations should keep refreshed sparse-store dimension");
}

void test_internal_materialized_session_flushes_after_other_sheet_edit_clears_rejected_error()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-other-edit-after-reject-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-other-edit-after-reject-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-after-cross-sheet-edit"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data(
            "Data", {{fastxlsx::CellValue::text("blocked-before-cross-sheet-edit")}});
    }), "same-sheet replacement should be rejected before a cross-sheet public edit");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedBeforeOtherEdit"); }),
        "same-sheet rename should be rejected before a cross-sheet public edit");
    check(editor.last_edit_error().has_value(),
        "rejected same-sheet operations should record a public edit diagnostic");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "rejected same-sheet operations should keep the materialized session dirty");

    editor.replace_sheet_data(
        "Untouched", {{fastxlsx::CellValue::text("other-sheet-after-rejected-op")}});

    check(!editor.last_edit_error().has_value(),
        "successful cross-sheet public edit should clear the rejected same-sheet diagnostic");
    check(editor.pending_change_count() == 1,
        "cross-sheet public replacement should queue one public edit before materialized flush");
    check(editor.pending_replacement_cell_count() == 1,
        "cross-sheet public replacement should expose only its own replacement diagnostics");
    {
        const std::vector<std::string> names = editor.pending_replacement_worksheet_names();
        check(names.size() == 1 && names[0] == "Untouched",
            "cross-sheet public replacement should not add diagnostics for the materialized sheet");
    }
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "successful cross-sheet public edit should not clear dirty materialized state");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 2,
        "cross-sheet replacement plus materialized flush should queue two coarse edits");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "materialized flush after cross-sheet public edit should clear dirty state");
    check(editor.pending_replacement_cell_count() == 1,
        "materialized flush should not pollute cross-sheet replacement diagnostics");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "cross-sheet edit plus materialized flush should preserve the materialized sheet name");
    check_contains(workbook_xml, R"(name="Untouched")",
        "cross-sheet edit plus materialized flush should preserve the other sheet name");
    check_not_contains(workbook_xml, "BlockedBeforeOtherEdit",
        "cross-sheet edit plus materialized flush should not leak the rejected rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "dirty-after-cross-sheet-edit",
        "cross-sheet edit plus materialized flush should save the materialized payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "blocked-before-cross-sheet-edit",
        "cross-sheet edit plus materialized flush should not leak the rejected same-sheet payload");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "other-sheet-after-rejected-op",
        "cross-sheet public replacement should still save its payload");
}

void test_internal_materialized_session_flushes_after_other_sheet_rename_clears_rejected_error()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-other-rename-after-reject-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-other-rename-after-reject-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-after-cross-sheet-rename"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data(
            "Data", {{fastxlsx::CellValue::text("blocked-before-cross-sheet-rename")}});
    }), "same-sheet replacement should be rejected before a cross-sheet public rename");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedBeforeOtherRename"); }),
        "same-sheet rename should be rejected before a cross-sheet public rename");
    check(editor.last_edit_error().has_value(),
        "rejected same-sheet operations should record a public edit diagnostic before rename");

    editor.rename_sheet("Untouched", "OtherRenamedAfterRejectedOp");

    check(!editor.last_edit_error().has_value(),
        "successful cross-sheet rename should clear the rejected same-sheet diagnostic");
    check(editor.pending_change_count() == 1,
        "cross-sheet rename should queue one public edit before materialized flush");
    check(editor.pending_replacement_cell_count() == 0,
        "cross-sheet rename should not create sheetData replacement diagnostics");
    check(editor.pending_replacement_worksheet_names().empty(),
        "cross-sheet rename should not create sheetData replacement names");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "successful cross-sheet rename should not clear dirty materialized state");
    {
        const std::vector<std::string> names = editor.worksheet_names();
        check(names.size() == 2 && names[0] == "Data" &&
                names[1] == "OtherRenamedAfterRejectedOp",
            "cross-sheet rename should update only the other planned sheet name");
    }

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 2,
        "cross-sheet rename plus materialized flush should queue two coarse edits");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "materialized flush after cross-sheet rename should clear dirty state");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "cross-sheet rename plus materialized flush should preserve the materialized sheet name");
    check_contains(workbook_xml, R"(name="OtherRenamedAfterRejectedOp")",
        "cross-sheet rename plus materialized flush should save the other sheet rename");
    check_not_contains(workbook_xml, "BlockedBeforeOtherRename",
        "cross-sheet rename plus materialized flush should not leak the rejected rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "dirty-after-cross-sheet-rename",
        "cross-sheet rename plus materialized flush should save the materialized payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "blocked-before-cross-sheet-rename",
        "cross-sheet rename plus materialized flush should not leak the rejected payload");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "cross-sheet rename should preserve the renamed worksheet bytes");
}

void test_internal_materialized_session_rejected_public_operations_preserve_flushed_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-rejected-after-flush-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-rejected-after-flush-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("flushed-before-rejected-public-op"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 1,
        "initial materialized flush should queue one staged worksheet projection");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "initial materialized flush should leave the private session clean");

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data(
            "Data", {{fastxlsx::CellValue::text("blocked-after-flush")}});
    }), "replace_sheet_data should remain rejected while a clean materialized session exists");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedAfterFlush"); }),
        "rename_sheet should remain rejected while a clean materialized session exists");

    check(editor.pending_change_count() == 1,
        "rejected public operations after flush should preserve the staged projection count");
    check(editor.pending_replacement_cell_count() == 0,
        "rejected public replacement after flush should not add replacement diagnostics");
    check(editor.pending_replacement_worksheet_names().empty(),
        "rejected public replacement after flush should not add replacement names");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "rejected public operations after flush should keep clean private state clean");
    check(editor.last_edit_error().has_value(),
        "rejected public operations after flush should record a public edit diagnostic");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "rejected public operations after flush should preserve the original sheet name");
    check_not_contains(workbook_xml, "BlockedAfterFlush",
        "rejected public operations after flush should not leak the rejected rename");
    check_contains(worksheet_xml, "flushed-before-rejected-public-op",
        "rejected public operations after flush should preserve the staged projection");
    check_not_contains(worksheet_xml, "blocked-after-flush",
        "rejected public operations after flush should not leak the rejected replacement");
}

void test_internal_materialized_session_failed_save_as_preserves_dirty_and_flushed_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-save-failure-source.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-materialized-save-failure-missing-parent") /
        "output.xlsx";
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-materialized-save-failure-directory-output");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-save-failure-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-save-failure-noop-output.xlsx");
    std::filesystem::create_directories(directory_output);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-survives-failed-save"));

    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path{}); }),
        "failed save_as before flush should throw without touching dirty materialized state");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "missing-parent save_as before flush should fail before materialized state changes");
    check_internal_materialized_session_save_state(
        editor, 1, 0, "failed save_as before flush");
    check(!editor.last_edit_error().has_value(),
        "failed save_as before flush should not create public last_edit_error");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed save_as before flush should not mutate the source package");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check_internal_materialized_session_save_state(
        editor, 0, 1, "explicit flush after failed save_as");

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "failed save_as over source should preserve staged materialized projection");
    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "failed save_as to directory should preserve staged materialized projection");
    check_internal_materialized_session_save_state(
        editor, 0, 1, "failed save_as after flush");
    check(!editor.last_edit_error().has_value(),
        "failed save_as after flush should not create public last_edit_error");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed save_as after flush should not mutate the source package");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "dirty-survives-failed-save",
        "valid save_as after failed saves should write the materialized projection");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "valid save_as after failed saves should keep refreshed sparse-store dimension");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "valid save_as after failed saves should not mutate the source package");

    editor.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after failed saves should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after failed saves should not mutate the source package");
}

void test_internal_materialized_session_reflush_after_failed_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-reflush-after-save-failure-source.xlsx");
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-save-failure-directory");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-save-failure-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-save-failure-noop.xlsx");
    std::filesystem::create_directories(directory_output);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("first-before-failed-save"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check_internal_materialized_session_save_state(
        editor, 0, 1, "initial flush before failed save_as");

    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "save_as to a directory should fail before consuming the staged projection");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed save_as before reflush should not mutate the source package");

    check_internal_materialized_session_save_state(
        editor, 0, 1, "failed save_as after initial flush");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("second-after-failed-save"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "mutating after failed save_as should dirty the existing materialized session again");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check_internal_materialized_session_save_state(
        editor, 0, 2, "reflush after failed save_as");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "second-after-failed-save",
        "reflush after failed save_as should save the later materialized payload");
    check_not_contains(worksheet_xml, "first-before-failed-save",
        "reflush after failed save_as should replace the earlier staged projection");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "reflush after failed save_as should not mutate the source package");

    editor.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after failed-save reflush should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after failed-save reflush should not mutate the source package");
}

void test_internal_materialized_session_move_reflush_after_failed_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-move-reflush-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-move-reflush-target.xlsx");
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-directory");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-noop-output.xlsx");
    std::filesystem::create_directories(directory_output);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto target_source_entries = fastxlsx::test::read_zip_entries(target_source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1,
        fastxlsx::CellValue::text("first-moved-before-failed-save"));
    editor.replace_sheet_data(
        "Untouched", {{fastxlsx::CellValue::text("assigned-public-survives-retry")}});

    fastxlsx::WorkbookEditor moved = std::move(editor);

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        target, "Untouched", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Untouched", 1, 1,
        fastxlsx::CellValue::text("discarded-target-materialized-retry"));
    target.replace_sheet_data(
        "Data", {{fastxlsx::CellValue::text("discarded-target-public-retry")}});

    target = std::move(moved);

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(target) == 1,
        "move assignment before failed save retry should keep one assigned materialized session");
    check_internal_materialized_session_save_state(
        target, 1, 1, "move assignment before failed save retry");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check_internal_materialized_session_save_state(
        target, 0, 2, "first flush after move assignment");

    check(threw_fastxlsx_error([&] { target.save_as(directory_output); }),
        "directory save_as after moved materialized flush should fail before consuming state");
    check_internal_materialized_session_save_state(
        target, 0, 2, "failed save after moved flush");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "moved failed save should not mutate the assigned source package");
    check(fastxlsx::test::read_zip_entries(target_source) == target_source_entries,
        "moved failed save should not mutate the discarded target source package");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Data", 1, 1,
        fastxlsx::CellValue::text("second-moved-after-failed-save"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 1,
        "mutation after moved failed save should re-dirty the assigned materialized session");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check_internal_materialized_session_save_state(
        target, 0, 3, "reflush after moved failed save");

    target.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_xml = output_entries.at("xl/worksheets/sheet2.xml");
    check_contains(data_xml, "second-moved-after-failed-save",
        "moved reflush after failed save should save the later materialized payload");
    check_not_contains(data_xml, "first-moved-before-failed-save",
        "moved reflush after failed save should replace the earlier staged projection");
    check_contains(untouched_xml, "assigned-public-survives-retry",
        "moved reflush after failed save should preserve assigned cross-sheet public edit");
    check_not_contains(data_xml, "discarded-target-public-retry",
        "moved reflush after failed save should not leak discarded target public edit");
    check_not_contains(untouched_xml, "discarded-target-materialized-retry",
        "moved reflush after failed save should not leak discarded target materialized session");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "moved reflush after failed save should not mutate the assigned source package");
    check(fastxlsx::test::read_zip_entries(target_source) == target_source_entries,
        "moved reflush after failed save should not mutate the discarded target source package");

    target.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after moved failed-save reflush should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after moved failed-save reflush should not mutate the assigned source package");
    check(fastxlsx::test::read_zip_entries(target_source) == target_source_entries,
        "clean no-op save_as after moved failed-save reflush should not mutate the discarded target source package");
}

void test_internal_materialized_session_reflush_after_successful_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-reflush-after-success-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-success-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-success-second.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-success-second-noop.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("first-before-successful-save"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "initial flush before successful save_as should leave materialized session clean");

    editor.save_as(first_output);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_worksheet_xml, "first-before-successful-save",
        "first successful save_as should write the initial materialized projection");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("second-after-successful-save"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "mutation after successful save_as should re-dirty the materialized session");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "reflush after successful save_as should clear dirty state again");
    check(editor.pending_change_count() == 2,
        "reflush after successful save_as should record a second coarse handoff");

    editor.save_as(second_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_worksheet_xml, "second-after-successful-save",
        "second successful save_as should write the later materialized projection");
    check_not_contains(second_worksheet_xml, "first-before-successful-save",
        "second successful save_as should not leak the earlier materialized projection");
    check_contains(first_worksheet_xml, "first-before-successful-save",
        "second successful save_as should not rewrite the first output artifact");

    editor.save_as(second_noop_output);
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == second_entries,
        "clean no-op save_as after reflush should be byte-stable");
}

void test_internal_materialized_session_move_reflush_after_successful_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-move-reflush-success-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-move-reflush-success-target.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-success-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-success-second.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-success-second-noop.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto target_source_entries = fastxlsx::test::read_zip_entries(target_source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1,
        fastxlsx::CellValue::text("first-moved-before-successful-save"));
    editor.replace_sheet_data(
        "Untouched", {{fastxlsx::CellValue::text("assigned-public-survives-success-reuse")}});

    fastxlsx::WorkbookEditor moved = std::move(editor);

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        target, "Untouched", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Untouched", 1, 1,
        fastxlsx::CellValue::text("discarded-target-materialized-success-reuse"));
    target.replace_sheet_data(
        "Data", {{fastxlsx::CellValue::text("discarded-target-public-success-reuse")}});

    target = std::move(moved);

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "first moved flush before successful save_as should clear dirty state");
    check(target.pending_change_count() == 2,
        "first moved flush before successful save_as should stage assigned public and materialized edits");

    target.save_as(first_output);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_data_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_untouched_xml = first_entries.at("xl/worksheets/sheet2.xml");
    check_contains(first_data_xml, "first-moved-before-successful-save",
        "first moved successful save_as should write the initial materialized projection");
    check_contains(first_untouched_xml, "assigned-public-survives-success-reuse",
        "first moved successful save_as should preserve the assigned cross-sheet public edit");
    check_not_contains(first_data_xml, "discarded-target-public-success-reuse",
        "first moved successful save_as should not leak discarded target public edit");
    check_not_contains(first_untouched_xml, "discarded-target-materialized-success-reuse",
        "first moved successful save_as should not leak discarded target materialized session");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "first moved successful save_as should not mutate the assigned source package");
    check(fastxlsx::test::read_zip_entries(target_source) == target_source_entries,
        "first moved successful save_as should not mutate the discarded target source package");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Data", 1, 1,
        fastxlsx::CellValue::text("second-moved-after-successful-save"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 1,
        "mutation after moved successful save_as should re-dirty assigned materialized state");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "reflush after moved successful save_as should clear dirty state again");
    check(target.pending_change_count() == 3,
        "reflush after moved successful save_as should record a new coarse handoff");

    target.save_as(second_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_data_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_untouched_xml = second_entries.at("xl/worksheets/sheet2.xml");
    check_contains(second_data_xml, "second-moved-after-successful-save",
        "second moved successful save_as should write the later materialized projection");
    check_not_contains(second_data_xml, "first-moved-before-successful-save",
        "second moved successful save_as should replace the earlier staged projection");
    check_contains(second_untouched_xml, "assigned-public-survives-success-reuse",
        "second moved successful save_as should keep the assigned cross-sheet public edit");
    check_not_contains(second_data_xml, "discarded-target-public-success-reuse",
        "second moved successful save_as should not leak discarded target public edit");
    check_not_contains(second_untouched_xml, "discarded-target-materialized-success-reuse",
        "second moved successful save_as should not leak discarded target materialized session");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "second moved successful save_as should not mutate the assigned source package");
    check(fastxlsx::test::read_zip_entries(target_source) == target_source_entries,
        "second moved successful save_as should not mutate the discarded target source package");

    const auto first_entries_after_second_save =
        fastxlsx::test::read_zip_entries(first_output);
    check(first_entries_after_second_save == first_entries,
        "second moved successful save_as should not rewrite the first output package");
    check(first_entries_after_second_save.at("xl/worksheets/sheet1.xml") == first_data_xml,
        "second moved successful save_as should not rewrite the first output worksheet");

    target.save_as(second_noop_output);
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == second_entries,
        "clean no-op save_as after moved reflush should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after moved reflush should not mutate the assigned source package");
    check(fastxlsx::test::read_zip_entries(target_source) == target_source_entries,
        "clean no-op save_as after moved reflush should not mutate the discarded target source package");
}

void test_internal_materialized_session_reflush_replaces_prior_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-reflush-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("first-flush"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "first materialized flush should clear dirty state");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("second-flush"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "mutation after first flush should re-dirty the materialized session");
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 2,
        "two dirty materialized flushes should count as two coarse internal edit handoffs");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "second materialized flush should clear dirty state again");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "second-flush",
        "second materialized flush should replace the prior staged projection");
    check_not_contains(worksheet_xml, "first-flush",
        "prior materialized flush payload should not leak after reflush");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "reflush output should keep the refreshed sparse-store dimension");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "reflush output should not mutate the source package");

    editor.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after replaced reflush should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after replaced reflush should not mutate the source package");
}

void test_internal_materialized_session_flush_failure_preserves_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-flush-failure-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-flush-failure-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Ghost", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("would-have-flushed"));
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Ghost", 1, 1, fastxlsx::CellValue::text("orphan-flush"));

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(
            editor);
    }), "materialized flush should reject a dirty session whose planned name is absent");
    check(editor.pending_change_count() == 0,
        "failed materialized flush should not queue coarse public edit diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 2,
        "failed materialized flush should preserve all dirty sessions");
    check_internal_materialized_flush_failure_dirty_projection_state(
        editor, "failed materialized flush");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed materialized flush should not mutate the source package");

    check(threw_fastxlsx_error([&] { editor.save_as(output); }),
        "public save_as auto-flush should also reject the missing planned-name projection");
    check(editor.pending_change_count() == 0,
        "failed save_as auto-flush should not queue partial materialized diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 2,
        "failed save_as auto-flush should preserve all dirty sessions");
    check_internal_materialized_flush_failure_dirty_projection_state(
        editor, "failed save_as auto-flush");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed save_as auto-flush should not mutate the source package");
}

void test_internal_materialized_session_flush_uses_planned_name_after_rename()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-flush-renamed-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-flush-renamed-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-flush-renamed-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "RenamedData");
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "RenamedData", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "RenamedData", 1, 1, fastxlsx::CellValue::text("renamed-flush"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 2,
        "rename plus materialized flush should queue two coarse edit diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "planned-name materialized flush after rename should clear dirty state");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "materialized flush after rename should not mutate the source package");
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "materialized flush after rename should preserve the planned catalog name");
    check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "materialized flush after rename should not restore the source catalog name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "renamed-flush",
        "materialized flush after rename should rewrite the source worksheet part");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<dimension ref="A1:B2"/>)",
        "materialized flush after rename should keep refreshed sparse-store dimension");

    editor.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after renamed materialized flush should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after renamed materialized flush should not mutate the source package");
}

void test_internal_materialized_session_blank_and_erase_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-erase-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-erase-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-erase-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");

    fastxlsx::detail::testing_workbook_editor_erase_materialized_cell(
        editor, "Data", 9, 9);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "erasing a missing materialized cell should keep the session clean");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::blank());
    fastxlsx::detail::testing_workbook_editor_erase_materialized_cell(
        editor, "Data", 2, 1);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "blank set plus existing-cell erase should dirty the materialized session");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "blank/erase materialized flush should clear dirty state");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "blank/erase materialized flush should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<row r="1"><c r="A1"/><c r="B1"><v>1</v></c></row>)",
        "explicit blank should remain as an empty A1 cell while B1 is preserved");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "blank materialized cell should remove prior source text payload");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "erased materialized source cell should not appear in flushed output");
    check_not_contains(worksheet_xml, R"(<row r="2")",
        "erasing the only row-2 source cell should remove the explicit row");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "blank/erase materialized flush should refresh dimension to remaining extents");

    editor.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after blank/erase materialized flush should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after blank/erase materialized flush should not mutate the source package");
}

void test_internal_materialized_session_repeated_materialize_preserves_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-rematerialize-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-rematerialize-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-rematerialize-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("kept-after-rematerialize"));

    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 1,
        "repeated materialization of the same planned sheet should reuse one session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "repeated materialization should preserve dirty state instead of reloading source");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "repeated materialization flush should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "kept-after-rematerialize",
        "repeated materialization should not discard dirty materialized payload");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "repeated materialization should not reload the original source A1 payload");

    editor.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after repeated materialization flush should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after repeated materialization flush should not mutate the source package");
}

void test_internal_materialized_session_load_guard_failure_preserves_editor_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-load-guard-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-load-guard-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-load-guard-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditorOptions options;
    options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source, options);

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "Data", "Data");
    }), "guarded materialized source load should fail before registering a session");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "failed materialized source load should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "failed materialized source load should not create dirty private state");
    check(!editor.has_pending_changes(),
        "failed materialized source load should not queue public edit diagnostics");
    check(editor.pending_change_count() == 0,
        "failed materialized source load should leave public pending count unchanged");
    check(!editor.last_edit_error().has_value(),
        "test-hook materialized source load failure should not update public last_edit_error");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed guarded materialized source load should not mutate the source package");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after-load-guard")}});
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "later valid replacement after guarded materialized load should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "after-load-guard",
        "editor should remain usable after guarded materialized load failure");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "later valid replacement should not preserve the old source A1 payload");

    editor.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after guarded materialized load recovery should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after guarded materialized load recovery should not mutate the source package");
}

void test_internal_materialized_session_memory_guard_failure_preserves_editor_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-memory-guard-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-memory-guard-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-memory-guard-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditorOptions options;
    options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source, options);

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "Data", "Data");
    }), "memory-guarded materialized source load should fail before registering a session");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "failed memory-guarded materialized load should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "failed memory-guarded materialized load should not create dirty private state");
    check(!editor.has_pending_changes(),
        "failed memory-guarded materialized load should not queue public edit diagnostics");
    check(!editor.last_edit_error().has_value(),
        "memory-guarded test-hook load failure should not update public last_edit_error");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed memory-guarded materialized source load should not mutate the source package");

    editor.rename_sheet("Data", "AfterMemoryGuard");
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "later rename after memory-guarded materialized load should not mutate the source package");
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="AfterMemoryGuard")",
        "editor should remain usable for rename after memory-guarded materialized load failure");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "rename after materialized load failure should preserve original worksheet data");

    editor.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after memory-guarded materialized load recovery should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after memory-guarded materialized load recovery should not mutate the source package");
}

void test_internal_materialized_session_missing_source_load_preserves_editor_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-missing-load-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-missing-load-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-materialized-missing-load-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "Missing", "Missing");
    }), "missing-source materialized load should fail before registering a session");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "missing-source materialized load should not register a private session");
    check(!fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Missing"),
        "failed missing-source load should not leave an orphan planned session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "missing-source materialized load should not create dirty private state");
    check(!editor.has_pending_changes(),
        "missing-source materialized load should not queue public edit diagnostics");
    check(editor.pending_change_count() == 0,
        "missing-source materialized load should leave public pending count unchanged");
    check(editor.pending_replacement_cell_count() == 0,
        "missing-source materialized load should leave replacement diagnostics unchanged");
    check(editor.pending_replacement_worksheet_names().empty(),
        "missing-source materialized load should not add pending replacement names");
    check(!editor.last_edit_error().has_value(),
        "test-hook missing-source load failure should not update public last_edit_error");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed missing-source materialized load should not mutate the source package");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after-missing-load")}});
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "later replacement after missing-source materialized load should not mutate the source package");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "after-missing-load",
        "editor should remain usable after missing-source materialized load failure");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "later valid replacement should not preserve the old source A1 payload");

    editor.save_as(noop_output);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "clean no-op save_as after missing-source materialized load recovery should be byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clean no-op save_as after missing-source materialized load recovery should not mutate the source package");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_zero_count_shifts_clear_diagnostics_preserve_state();
        test_public_worksheet_editor_disjoint_shifts_clear_diagnostics_preserve_dirty_state();
        test_public_worksheet_editor_boundary_shifts_clear_diagnostics_preserve_dirty_state();
        test_public_worksheet_editor_invalid_shifts_preserve_dirty_state_and_recover();
        test_public_worksheet_editor_shifted_state_survives_invalid_shift_recovery();
        test_public_worksheet_editor_delete_rows_columns_project_sparse_state();
        test_public_worksheet_editor_insert_rows_columns_project_sparse_state();
        test_public_worksheet_editor_insert_rows_columns_translate_moved_formula();
        test_public_worksheet_editor_delete_rows_columns_translate_moved_formula();
        test_public_worksheet_editor_insert_columns_rewrites_stationary_formula();
        test_public_worksheet_editor_insert_rows_rewrites_stationary_formula();
        test_public_worksheet_editor_insert_shifts_preserve_absolute_formula_markers();
        test_public_worksheet_editor_insert_shifts_rewrite_stationary_range_formulas();
        test_public_worksheet_editor_delete_shifts_rewrite_stationary_range_formulas();
        test_public_worksheet_editor_delete_shifts_preserve_absolute_formula_markers();
        test_public_worksheet_editor_delete_rows_rewrites_stationary_formula();
        test_public_worksheet_editor_delete_columns_rewrites_stationary_formula();
        test_internal_materialized_session_assignment_from_moved_from_source_clears_target();
        test_internal_materialized_session_blocks_whole_sheet_replacement();
        test_internal_materialized_session_blocks_materialize_after_public_replacement();
        test_internal_materialized_session_blocks_materialize_after_renamed_public_replacement();
        test_internal_materialized_session_blocks_materialize_after_replacement_on_renamed_sheet();
        test_internal_materialized_session_flushes_dirty_projection_to_patch_plan();
        test_internal_materialized_session_blocks_same_sheet_rename();
        test_internal_materialized_session_flushes_after_rejected_public_operations();
        test_internal_materialized_session_flushes_after_other_sheet_edit_clears_rejected_error();
        test_internal_materialized_session_flushes_after_other_sheet_rename_clears_rejected_error();
        test_internal_materialized_session_rejected_public_operations_preserve_flushed_projection();
        test_internal_materialized_session_failed_save_as_preserves_dirty_and_flushed_state();
        test_internal_materialized_session_reflush_after_failed_save_as();
        test_internal_materialized_session_move_reflush_after_failed_save_as();
        test_internal_materialized_session_reflush_after_successful_save_as();
        test_internal_materialized_session_move_reflush_after_successful_save_as();
        test_internal_materialized_session_reflush_replaces_prior_projection();
        test_internal_materialized_session_flush_failure_preserves_dirty_state();
        test_internal_materialized_session_flush_uses_planned_name_after_rename();
        test_internal_materialized_session_blank_and_erase_projection();
        test_internal_materialized_session_repeated_materialize_preserves_dirty_state();
        test_internal_materialized_session_load_guard_failure_preserves_editor_state();
        test_internal_materialized_session_memory_guard_failure_preserves_editor_state();
        test_internal_materialized_session_missing_source_load_preserves_editor_state();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor materialized-session check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor materialized-session tests passed\n");
    return 0;
}
