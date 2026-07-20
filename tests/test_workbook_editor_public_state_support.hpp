#pragma once

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

void check_cell_range_equals(
    const std::optional<fastxlsx::CellRange>& range,
    std::uint32_t first_row,
    std::uint32_t first_column,
    std::uint32_t last_row,
    std::uint32_t last_column,
    std::string_view message)
{
    check(range.has_value() && range->first_row == first_row &&
            range->first_column == first_column && range->last_row == last_row &&
            range->last_column == last_column,
        message);
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
    bool has_pending_changes{};
    std::size_t pending_change_count{};
    std::vector<std::string> pending_materialized_worksheet_names;
    std::size_t pending_materialized_cell_count{};
    std::size_t estimated_pending_materialized_memory_usage{};
    std::size_t pending_replacement_cell_count{};
    std::size_t estimated_pending_replacement_memory_usage{};
    std::vector<std::string> pending_replacement_worksheet_names;
    std::size_t pending_targeted_cell_replacement_count{};
    std::vector<std::string> pending_targeted_cell_replacement_worksheet_names;
    std::size_t estimated_pending_targeted_cell_replacement_xml_bytes{};
    std::optional<std::string> last_edit_error;
    std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> pending_worksheet_edits;
};

WorkbookEditorPublicSaveStateSnapshot workbook_editor_public_save_state_snapshot(
    const fastxlsx::WorkbookEditor& editor)
{
    return {
        editor.has_pending_changes(),
        editor.pending_change_count(),
        editor.pending_materialized_worksheet_names(),
        editor.pending_materialized_cell_count(),
        editor.estimated_pending_materialized_memory_usage(),
        editor.pending_replacement_cell_count(),
        editor.estimated_pending_replacement_memory_usage(),
        editor.pending_replacement_worksheet_names(),
        editor.pending_targeted_cell_replacement_count(),
        editor.pending_targeted_cell_replacement_worksheet_names(),
        editor.estimated_pending_targeted_cell_replacement_xml_bytes(),
        editor.last_edit_error(),
        editor.pending_worksheet_edits(),
    };
}

void check_workbook_editor_public_save_state_preserved(
    const fastxlsx::WorkbookEditor& editor,
    const WorkbookEditorPublicSaveStateSnapshot& before,
    std::string_view scenario)
{
    check(editor.has_pending_changes() == before.has_pending_changes,
        std::string(scenario) + " should preserve pending-change state");
    check(editor.pending_change_count() == before.pending_change_count,
        std::string(scenario) + " should preserve public pending change count");
    check(editor.pending_materialized_worksheet_names()
            == before.pending_materialized_worksheet_names,
        std::string(scenario) + " should preserve pending materialized worksheet names");
    check(editor.pending_materialized_cell_count()
            == before.pending_materialized_cell_count,
        std::string(scenario) + " should preserve pending materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage()
            == before.estimated_pending_materialized_memory_usage,
        std::string(scenario) + " should preserve materialized memory estimate");
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
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), before.pending_worksheet_edits),
        std::string(scenario) + " should preserve pending worksheet edit summaries");
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

std::filesystem::path artifact(std::string_view name)
{
    return fastxlsx::test::artifact_path(name);
}

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

std::filesystem::path write_two_sheet_source_with_large_clear_payload(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    const std::string large_a1 = "large-clear-a1-" + std::string(4096, 'a');
    const std::string large_a2 = "large-clear-a2-" + std::string(4096, 'b');

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text(large_a1),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text(large_a2),
            fastxlsx::CellView::text("clear-column-tail-b2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me")});
    }
    writer.close();

    return path;
}

std::filesystem::path write_two_sheet_source_with_large_clear_range_payload(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    const std::string large_a1 = "large-clear-range-a1-" + std::string(4096, 'a');
    const std::string large_a2 = "large-clear-range-a2-" + std::string(4096, 'b');

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text(large_a1),
            fastxlsx::CellView::number(1.0),
            fastxlsx::CellView::text("clear-range-c1")});
        data.append_row({fastxlsx::CellView::text(large_a2),
            fastxlsx::CellView::text("clear-range-b2"),
            fastxlsx::CellView::text("clear-range-c2")});
        data.append_row({fastxlsx::CellView::text("clear-range-a3"),
            fastxlsx::CellView::text("clear-range-b3"),
            fastxlsx::CellView::text("clear-range-c3")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me")});
    }
    writer.close();

    return path;
}

void check_reopened_clean_sheet_output(
    const std::filesystem::path& output,
    std::string_view sheet_name,
    std::string_view scenario,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet(sheet_name);
    const std::string prefix(scenario);

    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened output should not expose stale diagnostics");
    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened output should not expose dirty diagnostics");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened output should not expose dirty worksheet names");

    inspect(reopened_sheet);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened readback should keep public state clean");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened readback should keep dirty diagnostics empty");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened readback should keep dirty worksheet names empty");
    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened readback should keep last_edit_error empty");
}

void check_reopened_default_data_sheet_output(
    const std::filesystem::path& output,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check_reopened_clean_sheet_output(output, "Data", scenario,
        [prefix](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                prefix + " reopened output should keep default source sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                prefix + " reopened output should keep default source used range");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a1",
                prefix + " reopened output should read source-backed A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                prefix + " reopened output should read source-backed B1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "placeholder-a2",
                prefix + " reopened output should read source-backed A2");
        });
}

void check_public_state_single_named_dirty_materialized_summary(
    const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::string_view worksheet_name,
    std::size_t expected_pending_change_count,
    std::string_view scenario,
    const std::optional<std::string>& expected_last_edit_error = std::nullopt)
{
    const std::string prefix = std::string(scenario);
    const std::string expected_name = std::string(worksheet_name);
    const std::size_t expected_cell_count = sheet.cell_count();
    const std::size_t expected_memory_usage = sheet.estimated_memory_usage();

    check(editor.has_pending_changes(),
        prefix + " should expose pending public state");
    check(editor.pending_change_count() == expected_pending_change_count,
        prefix + " should not count dirty materialized sessions as staged handoffs");
    check(editor.last_edit_error() == expected_last_edit_error,
        prefix + " should expose the expected last_edit_error state");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not expose replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{expected_name},
        prefix + " should expose the expected dirty materialized worksheet");
    check(editor.pending_materialized_cell_count() == expected_cell_count,
        prefix + " should expose the dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == expected_memory_usage,
        prefix + " should expose the dirty materialized memory estimate");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        prefix + " should expose one dirty materialized summary");
    if (summaries.size() == 1) {
        const auto& summary = summaries[0];
        check(summary.source_name == expected_name &&
                summary.planned_name == expected_name &&
                !summary.renamed,
            prefix + " summary should identify the worksheet without rename state");
        check(!summary.sheet_data_replaced &&
                !summary.targeted_cells_replaced &&
                summary.replacement_cell_count == 0 &&
                summary.estimated_replacement_memory_usage == 0,
            prefix + " summary should not expose replacement state");
        check(summary.materialized_dirty &&
                summary.materialized_cell_count == expected_cell_count &&
                summary.estimated_materialized_memory_usage == expected_memory_usage,
            prefix + " summary should match the dirty materialized state");
    }
}

void check_public_state_single_data_dirty_materialized_summary(
    const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_pending_change_count,
    std::string_view scenario,
    const std::optional<std::string>& expected_last_edit_error = std::nullopt)
{
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Data", expected_pending_change_count, scenario,
        expected_last_edit_error);
}

void check_public_state_renamed_dirty_materialized_summary_memory(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& first_handle,
    fastxlsx::WorksheetEditor& second_handle,
    std::size_t expected_cell_count,
    std::size_t expected_memory_usage,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(first_handle.cell_count() == expected_cell_count &&
            second_handle.cell_count() == expected_cell_count,
        prefix + " should expose the expected sparse count on both handles");
    check(first_handle.estimated_memory_usage() == expected_memory_usage &&
            second_handle.estimated_memory_usage() == expected_memory_usage,
        prefix + " should expose the expected materialized memory on both handles");
    check(editor.pending_materialized_worksheet_names() ==
            std::vector<std::string>{"RenamedData"},
        prefix + " should report the renamed sheet as dirty materialized");
    check(editor.pending_materialized_cell_count() == expected_cell_count,
        prefix + " should report the expected dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == expected_memory_usage,
        prefix + " should report the expected dirty materialized memory");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        prefix + " should expose one renamed dirty materialized summary");
    if (summaries.size() == 1) {
        const auto& summary = summaries[0];
        check(summary.source_name == "Data" &&
                summary.planned_name == "RenamedData" &&
                summary.renamed,
            prefix + " summary should report the renamed worksheet");
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
}

void check_reopened_renamed_shift_noop_output(
    const std::filesystem::path& output,
    std::string_view scenario)
{
    check_reopened_clean_sheet_output(
        output, "RenamedData", scenario,
        [](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 3,
                "renamed shift no-op output should keep sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 3,
                "renamed shift no-op output should expose combined bounds");
            check(noop_sheet.get_cell("C1").number_value() == 1.0,
                "renamed shift no-op output should read shifted B1");
            check(noop_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "renamed shift no-op output should read shifted A2");
            check(!noop_sheet.try_cell("B1").has_value() &&
                    !noop_sheet.try_cell("A2").has_value(),
                "renamed shift no-op output should keep old coordinates absent");
        });
}

void check_reopened_shift_output(
    const std::filesystem::path& output,
    std::string_view scenario,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data");
    const std::string prefix(scenario);

    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened output should not expose stale diagnostics");
    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened output should not expose dirty diagnostics");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened output should not expose dirty worksheet names");

    inspect(reopened_sheet);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened readback should keep public state clean");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened readback should keep dirty diagnostics empty");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened readback should keep dirty worksheet names empty");
    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened readback should keep last_edit_error empty");
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

void check_shift_reacquire_retry_snapshots(
    fastxlsx::WorksheetEditor& snapshot_sheet,
    std::string_view scenario)
{
    const std::string label(scenario);
    const std::size_t baseline_cell_count = snapshot_sheet.cell_count();
    const std::size_t baseline_memory = snapshot_sheet.estimated_memory_usage();
    check(baseline_cell_count == 3,
        label + " cell_count should expose the combined shifted sparse count");
    check_cell_range_equals(snapshot_sheet.used_range(), 1, 1, 3, 3,
        label + " used_range should expose the combined shifted bounds");
    check(snapshot_sheet.contains_cell("A1") &&
            snapshot_sheet.contains_cell("C1") &&
            snapshot_sheet.contains_cell("A3"),
        label + " contains_cell should find shifted scalar coordinates");
    check(!snapshot_sheet.contains_cell("B1") &&
            !snapshot_sheet.contains_cell("A2") &&
            !snapshot_sheet.contains_cell("B2"),
        label + " contains_cell should keep old shifted coordinates absent");
    const std::optional<fastxlsx::CellValue> shifted_number =
        snapshot_sheet.try_cell("C1");
    check(shifted_number.has_value() &&
            shifted_number->kind() == fastxlsx::CellValueKind::Number &&
            shifted_number->number_value() == 1.0,
        label + " try_cell should expose shifted C1 number");
    const fastxlsx::CellValue shifted_text = snapshot_sheet.get_cell("A3");
    check(shifted_text.kind() == fastxlsx::CellValueKind::Text &&
            shifted_text.text_value() == "placeholder-a2",
        label + " get_cell should expose shifted A3 text");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        snapshot_sheet.sparse_cells();
    check(all_cells.size() == 3,
        label + " sparse_cells should expose the combined shifted sparse count");
    if (all_cells.size() == 3) {
        check(all_cells[0].reference.row == 1 && all_cells[0].reference.column == 1 &&
                all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                all_cells[0].value.text_value() == "placeholder-a1",
            label + " sparse_cells should keep A1 first");
        check(all_cells[1].reference.row == 1 && all_cells[1].reference.column == 3 &&
                all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                all_cells[1].value.number_value() == 1.0,
            label + " sparse_cells should expose shifted B1 as C1");
        check(all_cells[2].reference.row == 3 && all_cells[2].reference.column == 1 &&
                all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                all_cells[2].value.text_value() == "placeholder-a2",
            label + " sparse_cells should expose shifted A2 as A3");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
        snapshot_sheet.sparse_cells("A1:C3");
    check(shifted_range.size() == 3,
        label + " range sparse_cells should expose all combined shifted cells");
    if (shifted_range.size() == 3) {
        check(shifted_range[0].reference.row == 1 &&
                shifted_range[0].reference.column == 1 &&
                shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                shifted_range[0].value.text_value() == "placeholder-a1",
            label + " range sparse_cells should keep A1 first");
        check(shifted_range[1].reference.row == 1 &&
                shifted_range[1].reference.column == 3 &&
                shifted_range[1].value.kind() == fastxlsx::CellValueKind::Number &&
                shifted_range[1].value.number_value() == 1.0,
            label + " range sparse_cells should keep shifted C1 second");
        check(shifted_range[2].reference.row == 3 &&
                shifted_range[2].reference.column == 1 &&
                shifted_range[2].value.kind() == fastxlsx::CellValueKind::Text &&
                shifted_range[2].value.text_value() == "placeholder-a2",
            label + " range sparse_cells should keep shifted A3 third");
    }

    const std::array<fastxlsx::WorksheetCellReference, 6> requested_refs {
        fastxlsx::WorksheetCellReference {1, 3},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {3, 1},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {1, 3},
        fastxlsx::WorksheetCellReference {3, 1},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
        snapshot_sheet.sparse_cells(requested_refs);
    check(requested_cells.size() == 4,
        label + " requested sparse_cells should skip old shifted coordinates and keep duplicates");
    if (requested_cells.size() == 4) {
        check(requested_cells[0].reference.row == 1 &&
                requested_cells[0].reference.column == 3 &&
                requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                requested_cells[0].value.number_value() == 1.0,
            label + " requested sparse_cells should keep shifted C1 input order");
        check(requested_cells[1].reference.row == 3 &&
                requested_cells[1].reference.column == 1 &&
                requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[1].value.text_value() == "placeholder-a2",
            label + " requested sparse_cells should keep shifted A3 after skipped cells");
        check(requested_cells[2].reference.row == 1 &&
                requested_cells[2].reference.column == 3 &&
                requested_cells[2].value.kind() == fastxlsx::CellValueKind::Number &&
                requested_cells[2].value.number_value() == 1.0,
            label + " requested sparse_cells should preserve duplicate shifted C1");
        check(requested_cells[3].reference.row == 3 &&
                requested_cells[3].reference.column == 1 &&
                requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[3].value.text_value() == "placeholder-a2",
            label + " requested sparse_cells should preserve duplicate shifted A3");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        snapshot_sheet.row_cells(1);
    check(row_one.size() == 2,
        label + " row_cells should expose shifted row-one cells");
    if (row_one.size() == 2) {
        check(row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                row_one[0].value.text_value() == "placeholder-a1",
            label + " row_cells should keep A1 first");
        check(row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 3 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            label + " row_cells should keep shifted C1 second");
    }
    check(snapshot_sheet.row_cells(2).empty(),
        label + " row_cells should keep the inserted row gap empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
        snapshot_sheet.row_cells(3);
    check(row_three.size() == 1,
        label + " row_cells should expose the shifted row-three source cell");
    if (row_three.size() == 1) {
        check(row_three[0].reference.row == 3 &&
                row_three[0].reference.column == 1 &&
                row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                row_three[0].value.text_value() == "placeholder-a2",
            label + " row_cells should keep shifted A3 in row three");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        snapshot_sheet.column_cells(1);
    check(column_one.size() == 2,
        label + " column_cells should expose source and row-shifted cells");
    if (column_one.size() == 2) {
        check(column_one[0].reference.row == 1 &&
                column_one[0].reference.column == 1 &&
                column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                column_one[0].value.text_value() == "placeholder-a1",
            label + " column_cells should keep A1 first");
        check(column_one[1].reference.row == 3 &&
                column_one[1].reference.column == 1 &&
                column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_one[1].value.text_value() == "placeholder-a2",
            label + " column_cells should keep shifted A3 second");
    }
    check(snapshot_sheet.column_cells(2).empty(),
        label + " column_cells should keep the inserted column gap empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        snapshot_sheet.column_cells(3);
    check(column_three.size() == 1,
        label + " column_cells should expose the shifted numeric column");
    if (column_three.size() == 1) {
        check(column_three[0].reference.row == 1 &&
                column_three[0].reference.column == 3 &&
                column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_three[0].value.number_value() == 1.0,
            label + " column_cells should keep shifted C1 in column three");
    }
    check(!snapshot_sheet.has_pending_changes(),
        label + " read-only scalar and snapshot observers should keep the sheet clean");
    check(snapshot_sheet.cell_count() == baseline_cell_count,
        label + " read-only scalar and snapshot observers should preserve cell_count");
    check(snapshot_sheet.estimated_memory_usage() == baseline_memory,
        label + " read-only scalar and snapshot observers should preserve memory estimate");
    check_cell_range_equals(snapshot_sheet.used_range(), 1, 1, 3, 3,
        label + " read-only scalar and snapshot observers should preserve used_range");
}

} // namespace
