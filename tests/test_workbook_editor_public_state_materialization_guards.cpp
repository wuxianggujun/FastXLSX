#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, std::string_view message)
{
    if (!condition) {
        throw TestFailure(std::string(message));
    }
}

bool threw_fastxlsx_error(const std::function<void()>& action)
{
    try {
        action();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    }
    return false;
}

void check_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    check(haystack.find(needle) != std::string::npos, message);
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
    check(range.has_value() && range->first_row == first_row
            && range->first_column == first_column && range->last_row == last_row
            && range->last_column == last_column,
        message);
}

std::filesystem::path artifact(std::string_view filename)
{
    return fastxlsx::test::artifact_path(filename);
}

bool catalog_entries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& left,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].source_name != right[index].source_name
            || left[index].planned_name != right[index].planned_name
            || left[index].renamed != right[index].renamed) {
            return false;
        }
    }
    return true;
}

bool edit_summaries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& left,
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.source_name != rhs.source_name
            || lhs.planned_name != rhs.planned_name
            || lhs.renamed != rhs.renamed
            || lhs.sheet_data_replaced != rhs.sheet_data_replaced
            || lhs.targeted_cells_replaced != rhs.targeted_cells_replaced
            || lhs.replacement_cell_count != rhs.replacement_cell_count
            || lhs.estimated_replacement_memory_usage
                != rhs.estimated_replacement_memory_usage
            || lhs.materialized_dirty != rhs.materialized_dirty
            || lhs.materialized_cell_count != rhs.materialized_cell_count
            || lhs.estimated_materialized_memory_usage
                != rhs.estimated_materialized_memory_usage) {
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
    check(catalog_entries_equal(editor.worksheet_catalog(), before.catalog),
        prefix + " should preserve worksheet catalog");
}

struct WorkbookEditorPublicSaveStateSnapshot {
    bool has_pending_changes{};
    std::size_t pending_change_count{};
    std::vector<std::string> materialized_names;
    std::size_t materialized_cell_count{};
    std::size_t materialized_memory{};
    std::size_t replacement_cell_count{};
    std::size_t replacement_memory{};
    std::vector<std::string> replacement_names;
    std::size_t targeted_cell_count{};
    std::vector<std::string> targeted_names;
    std::size_t targeted_xml_bytes{};
    std::optional<std::string> last_edit_error;
    std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries;
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
    const WorkbookEditorPublicSaveStateSnapshot after =
        workbook_editor_public_save_state_snapshot(editor);
    const std::string prefix(scenario);
    check(after.has_pending_changes == before.has_pending_changes
            && after.pending_change_count == before.pending_change_count,
        prefix + " should preserve pending state");
    check(after.materialized_names == before.materialized_names
            && after.materialized_cell_count == before.materialized_cell_count
            && after.materialized_memory == before.materialized_memory,
        prefix + " should preserve materialized diagnostics");
    check(after.replacement_cell_count == before.replacement_cell_count
            && after.replacement_memory == before.replacement_memory
            && after.replacement_names == before.replacement_names,
        prefix + " should preserve replacement diagnostics");
    check(after.targeted_cell_count == before.targeted_cell_count
            && after.targeted_names == before.targeted_names
            && after.targeted_xml_bytes == before.targeted_xml_bytes,
        prefix + " should preserve targeted replacement diagnostics");
    check(after.last_edit_error == before.last_edit_error,
        prefix + " should preserve last_edit_error");
    check(edit_summaries_equal(after.summaries, before.summaries),
        prefix + " should preserve worksheet edit summaries");
}

void check_workbook_editor_no_replacement_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(editor.pending_replacement_cell_count() == 0
            && editor.estimated_pending_replacement_memory_usage() == 0
            && editor.pending_replacement_worksheet_names().empty(),
        prefix + " should expose no replacement diagnostics");
    check(editor.pending_targeted_cell_replacement_count() == 0
            && editor.pending_targeted_cell_replacement_worksheet_names().empty()
            && editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0
            && !editor.has_pending_targeted_cell_replacement("Data")
            && !editor.has_pending_targeted_cell_replacement("Styled"),
        prefix + " should expose no targeted replacement diagnostics");
}

void check_public_state_single_named_dirty_materialized_summary(
    const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::string_view worksheet_name,
    std::size_t expected_pending_change_count,
    std::string_view scenario,
    const std::optional<std::string>& expected_last_edit_error = std::nullopt)
{
    const std::string prefix(scenario);
    const std::string expected_name(worksheet_name);
    const std::size_t expected_cell_count = sheet.cell_count();
    const std::size_t expected_memory_usage = sheet.estimated_memory_usage();

    check(editor.has_pending_changes()
            && editor.pending_change_count() == expected_pending_change_count,
        prefix + " should expose dirty materialized public state");
    check(editor.last_edit_error() == expected_last_edit_error,
        prefix + " should expose expected last_edit_error");
    check_workbook_editor_no_replacement_diagnostics(editor, scenario);
    check(editor.pending_materialized_worksheet_names()
            == std::vector<std::string> {expected_name}
            && editor.pending_materialized_cell_count() == expected_cell_count
            && editor.estimated_pending_materialized_memory_usage()
                == expected_memory_usage,
        prefix + " should expose dirty materialized diagnostics");

    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        prefix + " should expose one dirty materialized summary");
    if (summaries.size() == 1) {
        const auto& summary = summaries[0];
        check(summary.source_name == expected_name
                && summary.planned_name == expected_name && !summary.renamed,
            prefix + " summary should identify the worksheet");
        check(!summary.sheet_data_replaced && !summary.targeted_cells_replaced
                && summary.replacement_cell_count == 0
                && summary.estimated_replacement_memory_usage == 0,
            prefix + " summary should expose no replacement state");
        check(summary.materialized_dirty
                && summary.materialized_cell_count == expected_cell_count
                && summary.estimated_materialized_memory_usage
                    == expected_memory_usage,
            prefix + " summary should match dirty materialized state");
    }
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

    check(!reopened_editor.last_edit_error().has_value()
            && !reopened_editor.has_pending_changes()
            && !reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize cleanly");
    check(reopened_editor.pending_change_count() == 0
            && reopened_editor.pending_materialized_cell_count() == 0
            && reopened_editor.estimated_pending_materialized_memory_usage() == 0
            && reopened_editor.pending_materialized_worksheet_names().empty()
            && reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened output should expose no materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_editor, prefix + " reopened output");

    inspect(reopened_sheet);

    check(!reopened_editor.last_edit_error().has_value()
            && !reopened_editor.has_pending_changes()
            && !reopened_sheet.has_pending_changes()
            && reopened_editor.pending_change_count() == 0
            && reopened_editor.pending_materialized_cell_count() == 0
            && reopened_editor.estimated_pending_materialized_memory_usage() == 0
            && reopened_editor.pending_materialized_worksheet_names().empty()
            && reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened readback should remain clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_editor, prefix + " reopened readback");
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


void check_workbook_editor_public_no_pending_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(!editor.has_pending_changes() && editor.pending_change_count() == 0
            && editor.pending_worksheet_edits().empty(),
        prefix + " should keep public pending state empty");
}

void check_reopened_default_data_sheet_output(
    const std::filesystem::path& output, std::string_view scenario)
{
    check_reopened_clean_sheet_output(
        output, "Data", scenario,
        [](fastxlsx::WorksheetEditor& sheet) {
            check(sheet.cell_count() == 3,
                "default Data output should keep three source cells");
            check_cell_range_equals(sheet.used_range(), 1, 1, 2, 2,
                "default Data output should keep source bounds");
            const auto cells = sheet.sparse_cells();
            check(cells.size() == 3
                    && cells[0].reference.row == 1
                    && cells[0].reference.column == 1
                    && cells[0].value.kind() == fastxlsx::CellValueKind::Text
                    && cells[0].value.text_value() == "placeholder-a1"
                    && cells[1].reference.row == 1
                    && cells[1].reference.column == 2
                    && cells[1].value.kind() == fastxlsx::CellValueKind::Number
                    && cells[1].value.number_value() == 1.0
                    && cells[2].reference.row == 2
                    && cells[2].reference.column == 1
                    && cells[2].value.kind() == fastxlsx::CellValueKind::Text
                    && cells[2].value.text_value() == "placeholder-a2",
                "default Data output should preserve source sparse values");
        });
}

void check_reopened_default_data_overwrite_output(
    const std::filesystem::path& output,
    std::string_view scenario,
    std::string_view expected_a1_text)
{
    const std::string prefix(scenario);
    const std::string expected_a1(expected_a1_text);
    check_reopened_clean_sheet_output(output, "Data", scenario,
        [prefix, expected_a1](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                prefix + " reopened output should keep source sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                prefix + " reopened output should keep source used range");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == expected_a1,
                prefix + " reopened output should read overwritten A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                prefix + " reopened output should keep source-backed B1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "placeholder-a2",
                prefix + " reopened output should keep source-backed A2");
            check(!reopened_sheet.try_cell("D4").has_value(),
                prefix + " reopened output should keep rejected D4 absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_cells =
                reopened_sheet.sparse_cells();
            check(reopened_cells.size() == 3,
                prefix + " reopened sparse_cells should expose all represented cells");
            if (reopened_cells.size() == 3) {
                check(reopened_cells[0].reference.row == 1 &&
                        reopened_cells[0].reference.column == 1 &&
                        reopened_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_cells[0].value.text_value() == expected_a1,
                    prefix + " reopened sparse_cells should expose overwritten A1 first");
                check(reopened_cells[1].reference.row == 1 &&
                        reopened_cells[1].reference.column == 2 &&
                        reopened_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_cells[1].value.number_value() == 1.0,
                    prefix + " reopened sparse_cells should expose source B1 second");
                check(reopened_cells[2].reference.row == 2 &&
                        reopened_cells[2].reference.column == 1 &&
                        reopened_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_cells[2].value.text_value() == "placeholder-a2",
                    prefix + " reopened sparse_cells should expose source A2 last");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_range_cells =
                reopened_sheet.sparse_cells("A1:D4");
            check(reopened_range_cells.size() == 3,
                prefix + " reopened range sparse_cells should expose all represented cells");
            if (reopened_range_cells.size() == 3) {
                check(reopened_range_cells[0].reference.row == 1 &&
                        reopened_range_cells[0].reference.column == 1 &&
                        reopened_range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_range_cells[0].value.text_value() == expected_a1,
                    prefix + " reopened range sparse_cells should expose overwritten A1 first");
                check(reopened_range_cells[1].reference.row == 1 &&
                        reopened_range_cells[1].reference.column == 2 &&
                        reopened_range_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_range_cells[1].value.number_value() == 1.0,
                    prefix + " reopened range sparse_cells should expose source B1 second");
                check(reopened_range_cells[2].reference.row == 2 &&
                        reopened_range_cells[2].reference.column == 1 &&
                        reopened_range_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_range_cells[2].value.text_value() == "placeholder-a2",
                    prefix + " reopened range sparse_cells should expose source A2 last");
            }
            const std::array<fastxlsx::WorksheetCellReference, 6> reopened_requested_refs {
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {4, 4},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_requested_cells =
                reopened_sheet.sparse_cells(reopened_requested_refs);
            check(reopened_requested_cells.size() == 4,
                prefix + " reopened requested sparse_cells should skip rejected/gap coordinates and keep duplicates");
            if (reopened_requested_cells.size() == 4) {
                check(reopened_requested_cells[0].reference.row == 2 &&
                        reopened_requested_cells[0].reference.column == 1 &&
                        reopened_requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_requested_cells[0].value.text_value() == "placeholder-a2",
                    prefix + " reopened requested sparse_cells should keep A2 first");
                check(reopened_requested_cells[1].reference.row == 1 &&
                        reopened_requested_cells[1].reference.column == 2 &&
                        reopened_requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_requested_cells[1].value.number_value() == 1.0,
                    prefix + " reopened requested sparse_cells should keep B1 after skipped D4");
                check(reopened_requested_cells[2].reference.row == 1 &&
                        reopened_requested_cells[2].reference.column == 1 &&
                        reopened_requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_requested_cells[2].value.text_value() == expected_a1,
                    prefix + " reopened requested sparse_cells should keep overwritten A1 in requested order");
                check(reopened_requested_cells[3].reference.row == 2 &&
                        reopened_requested_cells[3].reference.column == 1 &&
                        reopened_requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_requested_cells[3].value.text_value() == "placeholder-a2",
                    prefix + " reopened requested sparse_cells should preserve duplicate A2");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                reopened_sheet.row_cells(1);
            check(reopened_row_one.size() == 2 &&
                    reopened_row_one[0].reference.row == 1 &&
                    reopened_row_one[0].reference.column == 1 &&
                    reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[0].value.text_value() == expected_a1 &&
                    reopened_row_one[1].reference.row == 1 &&
                    reopened_row_one[1].reference.column == 2 &&
                    reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_row_one[1].value.number_value() == 1.0,
                prefix + " reopened row_cells should expose overwritten A1 and source B1");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 1 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "placeholder-a2",
                prefix + " reopened row_cells should expose source A2");
            check(reopened_sheet.row_cells(3).empty(),
                prefix + " reopened row_cells should keep the gap row empty");
            check(reopened_sheet.row_cells(4).empty(),
                prefix + " reopened row_cells should keep rejected D4 row empty");

            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_one =
                reopened_sheet.column_cells(1);
            check(reopened_column_one.size() == 2 &&
                    reopened_column_one[0].reference.row == 1 &&
                    reopened_column_one[0].reference.column == 1 &&
                    reopened_column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_one[0].value.text_value() == expected_a1 &&
                    reopened_column_one[1].reference.row == 2 &&
                    reopened_column_one[1].reference.column == 1 &&
                    reopened_column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_one[1].value.text_value() == "placeholder-a2",
                prefix + " reopened column_cells should expose overwritten A1 and source A2");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_two =
                reopened_sheet.column_cells(2);
            check(reopened_column_two.size() == 1 &&
                    reopened_column_two[0].reference.row == 1 &&
                    reopened_column_two[0].reference.column == 2 &&
                    reopened_column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_column_two[0].value.number_value() == 1.0,
                prefix + " reopened column_cells should expose source B1");
            check(reopened_sheet.column_cells(3).empty(),
                prefix + " reopened column_cells should keep the gap column empty");
            check(reopened_sheet.column_cells(4).empty(),
                prefix + " reopened column_cells should keep rejected D4 absent");
        });
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

void test_public_worksheet_editor_options_guard_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-options-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-options-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-options-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-options-second-noop-output.xlsx");
    const std::filesystem::path strict_options_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-options-strict-noop-output.xlsx");
    const std::filesystem::path strict_options_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-options-strict-post-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

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
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed public WorksheetEditor materialization should not leave a partial materialized session");
    check(editor.pending_materialized_cell_count() == 0,
        "failed public WorksheetEditor materialization should not expose partial materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed public WorksheetEditor materialization should not expose partial materialized memory");
    check(!editor.last_edit_error().has_value(),
        "failed public WorksheetEditor materialization should not update last_edit_error");
    check_workbook_editor_public_clean_state(
        editor, "failed public WorksheetEditor materialization");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed public WorksheetEditor materialization should leave the source package unchanged");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after-options-failure")}});
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "options guard recovery save_as should leave the source package unchanged");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "after-options-failure",
        "editor should remain usable after failed public WorksheetEditor materialization");
    const auto check_options_guard_single_cell_snapshots =
        [](fastxlsx::WorksheetEditor& observed_sheet,
           std::string_view scenario,
           std::string_view expected_text_view) {
            const std::string prefix(scenario);
            const std::string expected_text(expected_text_view);

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                observed_sheet.sparse_cells();
            check(cells.size() == 1,
                prefix + " sparse_cells should expose only replacement A1");
            if (cells.size() == 1) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == expected_text,
                    prefix + " sparse_cells should keep replacement A1");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
                observed_sheet.sparse_cells("A1:B2");
            check(range_cells.size() == 1,
                prefix + " range sparse_cells should skip old source cells and gaps");
            if (range_cells.size() == 1) {
                check(range_cells[0].reference.row == 1 &&
                        range_cells[0].reference.column == 1 &&
                        range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        range_cells[0].value.text_value() == expected_text,
                    prefix + " range sparse_cells should keep replacement A1");
            }

            const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {1, 1},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                observed_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 2,
                prefix + " requested sparse_cells should skip old source cells and keep duplicate A1");
            if (requested_cells.size() == 2) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 1 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[0].value.text_value() == expected_text,
                    prefix + " requested sparse_cells should keep replacement A1 first");
                check(requested_cells[1].reference.row == 1 &&
                        requested_cells[1].reference.column == 1 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == expected_text,
                    prefix + " requested sparse_cells should preserve duplicate A1");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                observed_sheet.row_cells(1);
            check(row_one.size() == 1 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == expected_text,
                prefix + " row_cells should expose replacement A1 only");
            check(observed_sheet.row_cells(2).empty(),
                prefix + " row_cells should not revive old source A2");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                observed_sheet.column_cells(1);
            check(column_one.size() == 1 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[0].value.text_value() == expected_text,
                prefix + " column_cells should expose replacement A1 only");
            check(observed_sheet.column_cells(2).empty(),
                prefix + " column_cells should not revive old source B1");
        };
    const auto inspect_reopened_output =
        [&check_options_guard_single_cell_snapshots](
            fastxlsx::WorksheetEditor& reopened_sheet) {
        check(reopened_sheet.cell_count() == 1,
            "options guard recovery reopened output should expose replacement count");
        check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 1,
            "options guard recovery reopened output should expose replacement range");
        const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
        check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                reopened_a1.text_value() == "after-options-failure",
            "options guard recovery reopened output should read replacement A1");
        check(!reopened_sheet.try_cell("B1").has_value() &&
                !reopened_sheet.try_cell("A2").has_value(),
            "options guard recovery reopened output should not keep old source cells");
        check_options_guard_single_cell_snapshots(
            reopened_sheet,
            "options guard recovery reopened output",
            "after-options-failure");
    };
    const auto inspect_strict_post_noop_output =
        [&check_options_guard_single_cell_snapshots](
            fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 1,
                "options guard recovery strict post-noop output should expose replacement count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 1,
                "options guard recovery strict post-noop output should expose replacement range");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "after-options-strict-reacquire",
                "options guard recovery strict post-noop output should read overwritten A1");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "options guard recovery strict post-noop output should not revive old source cells");
            check_options_guard_single_cell_snapshots(
                reopened_sheet,
                "options guard recovery strict post-noop output",
                "after-options-strict-reacquire");
        };
    check_reopened_clean_sheet_output(output, "Data", "options guard recovery",
        inspect_reopened_output);

    fastxlsx::WorkbookEditor strict_options_editor =
        fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor strict_options_sheet =
        strict_options_editor.worksheet("Data", options);
    inspect_reopened_output(strict_options_sheet);
    check(!strict_options_sheet.has_pending_changes(),
        "options guard recovery strict-options reacquire should materialize a clean sheet");
    check_workbook_editor_public_clean_state(
        strict_options_editor,
        "options guard recovery strict-options reacquire");
    strict_options_editor.save_as(strict_options_noop_output);
    check(!strict_options_sheet.has_pending_changes(),
        "options guard recovery strict-options no-op save should keep the sheet clean");
    check_workbook_editor_public_clean_state(
        strict_options_editor,
        "options guard recovery strict-options no-op save");
    const auto strict_options_noop_entries =
        fastxlsx::test::read_zip_entries(strict_options_noop_output);
    check(strict_options_noop_entries == output_entries,
        "options guard recovery strict-options no-op output should match the saved output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "options guard recovery strict-options no-op save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(strict_options_noop_output, "Data",
        "options guard recovery strict-options no-op",
        inspect_reopened_output);

    strict_options_sheet.set_cell(
        "A1", fastxlsx::CellValue::text("after-options-strict-reacquire"));
    check(strict_options_sheet.has_pending_changes(),
        "options guard recovery strict post-noop overwrite should dirty the reacquired sheet");
    check(strict_options_editor.has_pending_changes(),
        "options guard recovery strict post-noop overwrite should dirty the reacquired editor");
    check(strict_options_editor.pending_materialized_worksheet_names() ==
            std::vector<std::string>{"Data"},
        "options guard recovery strict post-noop overwrite should expose dirty Data");
    check(strict_options_editor.pending_materialized_cell_count() == 1,
        "options guard recovery strict post-noop overwrite should stay within max_cells");
    check_workbook_editor_no_replacement_diagnostics(
        strict_options_editor,
        "options guard recovery strict post-noop overwrite");
    check(!strict_options_editor.last_edit_error().has_value(),
        "options guard recovery strict post-noop overwrite should keep diagnostics clear");
    check_options_guard_single_cell_snapshots(
        strict_options_sheet,
        "options guard recovery strict post-noop overwrite",
        "after-options-strict-reacquire");
    check_public_state_single_data_dirty_materialized_summary(
        strict_options_editor,
        strict_options_sheet,
        0,
        "options guard recovery strict post-noop overwrite");

    strict_options_editor.save_as(strict_options_post_noop_output);
    check(!strict_options_sheet.has_pending_changes(),
        "options guard recovery strict post-noop save should clean the reacquired sheet");
    check(strict_options_editor.pending_change_count() == 1,
        "options guard recovery strict post-noop save should record one materialized handoff");
    check(strict_options_editor.pending_materialized_worksheet_names().empty(),
        "options guard recovery strict post-noop save should clear dirty materialized names");
    check(strict_options_editor.pending_materialized_cell_count() == 0,
        "options guard recovery strict post-noop save should clear dirty materialized cells");
    check(strict_options_editor.estimated_pending_materialized_memory_usage() == 0,
        "options guard recovery strict post-noop save should clear dirty materialized memory");
    check(strict_options_editor.pending_worksheet_edits().empty(),
        "options guard recovery strict post-noop save should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        strict_options_editor,
        "options guard recovery strict post-noop save");
    check(!strict_options_editor.last_edit_error().has_value(),
        "options guard recovery strict post-noop save should keep diagnostics clear");
    const auto strict_options_post_noop_entries =
        fastxlsx::test::read_zip_entries(strict_options_post_noop_output);
    const std::string strict_options_post_noop_xml =
        strict_options_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(strict_options_post_noop_xml, "after-options-strict-reacquire",
        "options guard recovery strict post-noop save should persist the overwrite");
    check_not_contains(strict_options_post_noop_xml, "after-options-failure",
        "options guard recovery strict post-noop save should replace the old value");
    check(strict_options_noop_entries == output_entries,
        "options guard recovery strict post-noop save should leave the no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "options guard recovery strict post-noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(strict_options_post_noop_output, "Data",
        "options guard recovery strict post-noop",
        inspect_strict_post_noop_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_noop =
        editor.pending_worksheet_edits();
    editor.save_as(noop_output);
    check(editor.pending_change_count() == 1,
        "options guard recovery noop save should not add a second public handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "options guard recovery noop save should not leave dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "options guard recovery noop save should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "options guard recovery noop save should not leave dirty materialized memory");
    check(editor.has_pending_replacement("Data"),
        "options guard recovery noop save should retain the queued Data replacement");
    check(editor.pending_replacement_worksheet_names() ==
            std::vector<std::string>{"Data"},
        "options guard recovery noop save should keep replacement names stable");
    check(editor.pending_replacement_cell_count() == 1,
        "options guard recovery noop save should keep replacement cell diagnostics");
    check(editor.estimated_pending_replacement_memory_usage() > 0,
        "options guard recovery noop save should keep replacement memory diagnostics");
    check(!editor.last_edit_error().has_value(),
        "options guard recovery noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "options guard recovery noop save");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_noop),
        "options guard recovery noop save should preserve pending summaries");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "options guard recovery noop save");

    const auto noop_output_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_output_entries == output_entries,
        "options guard recovery noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "options guard recovery noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(noop_output, "Data", "options guard recovery noop",
        inspect_reopened_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_second_noop =
        editor.pending_worksheet_edits();
    editor.save_as(second_noop_output);
    check(editor.pending_change_count() == 1,
        "options guard recovery second noop save should not add another public handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "options guard recovery second noop save should not leave dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "options guard recovery second noop save should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "options guard recovery second noop save should not leave dirty materialized memory");
    check(editor.has_pending_replacement("Data"),
        "options guard recovery second noop save should retain the queued Data replacement");
    check(editor.pending_replacement_worksheet_names() ==
            std::vector<std::string>{"Data"},
        "options guard recovery second noop save should keep replacement names stable");
    check(editor.pending_replacement_cell_count() == 1,
        "options guard recovery second noop save should keep replacement cell diagnostics");
    check(editor.estimated_pending_replacement_memory_usage() > 0,
        "options guard recovery second noop save should keep replacement memory diagnostics");
    check(!editor.last_edit_error().has_value(),
        "options guard recovery second noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "options guard recovery second noop save");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_second_noop),
        "options guard recovery second noop save should preserve pending summaries");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "options guard recovery second noop save");

    const auto second_noop_output_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_output_entries == noop_output_entries,
        "options guard recovery second noop output should match the first noop output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_output_entries,
        "options guard recovery second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "options guard recovery second noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(second_noop_output, "Data",
        "options guard recovery second noop",
        inspect_reopened_output);
}

void test_public_worksheet_editor_memory_budget_guard_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-memory-options-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-memory-options-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-memory-options-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-memory-options-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

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
    check_workbook_editor_public_clean_state(
        editor, "memory-budget materialization failure");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "memory-budget materialization failure should leave the source package unchanged");

    std::optional<fastxlsx::WorksheetEditor> recovered = editor.try_worksheet("Data");
    check(recovered.has_value(),
        "editor should remain able to materialize the sheet after memory-budget failure");
    check(recovered.has_value() && recovered->cell_count() == 3,
        "recovered materialization should load all source cells after memory-budget failure");
    if (recovered.has_value()) {
        recovered->set_cell("A1", fastxlsx::CellValue::text("after-memory-budget-failure"));
        const std::size_t recovered_memory = recovered->estimated_memory_usage();
        check(recovered->has_pending_changes(),
            "recovered materialization overwrite should dirty the recovered session");
        check(editor.has_pending_changes(),
            "recovered materialization overwrite should dirty the editor");
        check(editor.pending_materialized_cell_count() == recovered->cell_count(),
            "recovered materialization overwrite should expose recovered sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == recovered_memory,
            "recovered materialization overwrite should expose recovered materialized memory");
        check_public_state_single_data_dirty_materialized_summary(
            editor, *recovered, 0, "memory-budget source-load recovery overwrite");
    }
    editor.save_as(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "memory-budget source-load recovery save_as should leave the source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "after-memory-budget-failure",
        "recovered WorksheetEditor session should save after memory-budget failure");
    check_reopened_default_data_overwrite_output(output, "memory-budget source-load recovery",
        "after-memory-budget-failure");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(recovered.has_value() && !recovered->has_pending_changes(),
        "memory-budget source-load recovery noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "memory-budget source-load recovery noop save should not add a second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "memory-budget source-load recovery noop save should not leave dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "memory-budget source-load recovery noop save should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "memory-budget source-load recovery noop save should not leave dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "memory-budget source-load recovery noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "memory-budget source-load recovery noop save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "memory-budget source-load recovery noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "memory-budget source-load recovery noop save");

    const auto noop_output_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_output_entries == output_entries,
        "memory-budget source-load recovery noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "memory-budget source-load recovery noop save should leave the source package unchanged");
    check_reopened_default_data_overwrite_output(noop_output, "memory-budget source-load recovery noop",
        "after-memory-budget-failure");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(recovered.has_value() && !recovered->has_pending_changes(),
        "memory-budget source-load recovery second noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "memory-budget source-load recovery second noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "memory-budget source-load recovery second noop save should not leave dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "memory-budget source-load recovery second noop save should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "memory-budget source-load recovery second noop save should not leave dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "memory-budget source-load recovery second noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "memory-budget source-load recovery second noop save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "memory-budget source-load recovery second noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "memory-budget source-load recovery second noop save");

    const auto second_noop_output_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_output_entries == noop_output_entries,
        "memory-budget source-load recovery second noop output should match the first noop output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_output_entries,
        "memory-budget source-load recovery second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "memory-budget source-load recovery second noop save should leave the source package unchanged");
    check_reopened_default_data_overwrite_output(second_noop_output,
        "memory-budget source-load recovery second noop",
        "after-memory-budget-failure");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_options_guard_failure_preserves_state();
        test_public_worksheet_editor_memory_budget_guard_failure_preserves_state();
        std::cout << "WorkbookEditor public-state materialization guard tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state materialization guard test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
