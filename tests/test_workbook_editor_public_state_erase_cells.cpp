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

void test_public_worksheet_editor_erase_cell_auto_flushes_on_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-erase-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-output.xlsx");
    const std::filesystem::path reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-reacquired-noop-output.xlsx");
    const std::filesystem::path reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-reacquired-second-noop-output.xlsx");
    const std::filesystem::path reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-reacquired-post-noop-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(sheet.cell_count() == 3,
        "public WorksheetEditor should materialize the supported source cells");
    sheet.erase_cell(2, 1);
    check(!sheet.try_cell(2, 1).has_value(),
        "public WorksheetEditor erase_cell should remove the sparse record");
    check(sheet.cell_count() == 2,
        "public WorksheetEditor erase_cell should update sparse cell count");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "erase_cell dirty summary");

    const auto check_reopened_erase_cell_projection =
        [](fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 2,
                prefix + " reopened output should keep remaining sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 2,
                prefix + " reopened output should keep shrunk bounds");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 2,
                prefix + " reopened sparse_cells should expose two cells");
            if (cells.size() == 2) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "placeholder-a1",
                    prefix + " reopened sparse_cells should keep A1 first");
                check(cells[1].reference.row == 1 &&
                        cells[1].reference.column == 2 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[1].value.number_value() == 1.0,
                    prefix + " reopened sparse_cells should keep B1 second");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "placeholder-a1" &&
                    row_one[1].reference.row == 1 &&
                    row_one[1].reference.column == 2 &&
                    row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    row_one[1].value.number_value() == 1.0,
                prefix + " reopened row_cells should expose A1 then B1");
            check(reopened_sheet.row_cells(2).empty(),
                prefix + " reopened row_cells should keep erased row two empty");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 1 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[0].value.text_value() == "placeholder-a1",
                prefix + " reopened column_cells should expose A1 only");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 1 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_two[0].value.number_value() == 1.0,
                prefix + " reopened column_cells should expose B1 only");

            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a1",
                prefix + " reopened output should keep source-backed A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                prefix + " reopened output should keep source-backed B1");
            check(!reopened_sheet.try_cell("A2").has_value(),
                prefix + " reopened output should keep erased A2 absent");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "erase_cell save should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "public WorksheetEditor erase_cell should persist through save_as");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "public WorksheetEditor erase_cell should shrink the projected dimension");
    check_reopened_clean_sheet_output(output, "Data", "erase_cell auto flush",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_erase_cell_projection(
                reopened_sheet, "erase_cell auto flush");
        });

    fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reacquired_sheet =
        reacquired_editor.worksheet("Data");
    const auto check_reacquired_editor_clean_state =
        [&](std::string_view scenario) {
            const std::string prefix(scenario);

            check(!reacquired_editor.last_edit_error().has_value(),
                prefix + " should not expose stale diagnostics");
            check(!reacquired_editor.has_pending_changes(),
                prefix + " should keep editor state clean");
            check(reacquired_editor.pending_change_count() == 0 &&
                    reacquired_editor.pending_materialized_cell_count() == 0 &&
                    reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
                    reacquired_editor.pending_replacement_cell_count() == 0 &&
                    reacquired_editor.estimated_pending_replacement_memory_usage() == 0 &&
                    reacquired_editor.pending_worksheet_edits().empty(),
                prefix + " should not expose dirty diagnostics");
            check(reacquired_editor.pending_materialized_worksheet_names().empty() &&
                    reacquired_editor.pending_replacement_worksheet_names().empty(),
                prefix + " should not expose dirty worksheet names");
        };
    check_reacquired_editor_clean_state(
        "erase_cell auto flush saved-output reacquire");
    check(!reacquired_sheet.has_pending_changes(),
        "erase_cell saved-output reacquire should keep the sheet clean");
    check_reopened_erase_cell_projection(
        reacquired_sheet, "erase_cell saved-output reacquire");

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_noop,
        "erase_cell saved-output reacquired noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_noop,
        "erase_cell saved-output reacquired noop save");
    check_reacquired_editor_clean_state(
        "erase_cell auto flush saved-output reacquired noop save");
    check(!reacquired_sheet.has_pending_changes(),
        "erase_cell saved-output reacquired noop save should keep the sheet clean");
    check_reopened_erase_cell_projection(
        reacquired_sheet, "erase_cell saved-output reacquired noop save");
    const auto reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_noop_output);
    check(reacquired_noop_entries == output_entries,
        "erase_cell saved-output reacquired noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "erase_cell saved-output reacquired noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(
        reacquired_noop_output, "Data",
        "erase_cell saved-output reacquired noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_erase_cell_projection(
                reopened_sheet,
                "erase_cell saved-output reacquired noop save");
        });

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_second_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_second_noop,
        "erase_cell saved-output reacquired second noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_second_noop,
        "erase_cell saved-output reacquired second noop save");
    check_reacquired_editor_clean_state(
        "erase_cell auto flush saved-output reacquired second noop save");
    check(!reacquired_sheet.has_pending_changes(),
        "erase_cell saved-output reacquired second noop save should keep the sheet clean");
    check_reopened_erase_cell_projection(
        reacquired_sheet, "erase_cell saved-output reacquired second noop save");
    const auto reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_second_noop_output);
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "erase_cell saved-output reacquired second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "erase_cell saved-output reacquired second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "erase_cell saved-output reacquired second noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(
        reacquired_second_noop_output, "Data",
        "erase_cell saved-output reacquired second noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_erase_cell_projection(
                reopened_sheet,
                "erase_cell saved-output reacquired second noop save");
        });

    reacquired_sheet.set_cell("A1",
        fastxlsx::CellValue::text("erase-cell-final"));
    check(!reacquired_editor.last_edit_error().has_value(),
        "erase_cell saved-output reacquired post-noop edit should keep diagnostics clear");
    check(reacquired_sheet.has_pending_changes(),
        "erase_cell saved-output reacquired post-noop edit should dirty the sheet");
    check(reacquired_editor.has_pending_changes(),
        "erase_cell saved-output reacquired post-noop edit should dirty the editor");
    check(reacquired_sheet.cell_count() == 2,
        "erase_cell saved-output reacquired post-noop edit should keep sparse count stable");
    check(reacquired_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text &&
            reacquired_sheet.get_cell("A1").text_value() == "erase-cell-final",
        "erase_cell saved-output reacquired post-noop edit should overwrite surviving A1");
    check(!reacquired_sheet.try_cell("A2").has_value(),
        "erase_cell saved-output reacquired post-noop edit should keep erased A2 absent");
    check_public_state_single_data_dirty_materialized_summary(
        reacquired_editor,
        reacquired_sheet,
        0,
        "erase_cell saved-output reacquired post-noop edit");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "erase_cell saved-output reacquired post-noop edit should not queue replacement diagnostics");

    reacquired_editor.save_as(reacquired_post_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "erase_cell saved-output reacquired post-noop save should clean the sheet");
    check(reacquired_editor.pending_change_count() == 1,
        "erase_cell saved-output reacquired post-noop save should keep one materialized handoff");
    check(reacquired_editor.pending_materialized_worksheet_names().empty(),
        "erase_cell saved-output reacquired post-noop save should not expose dirty worksheet names");
    check(reacquired_editor.pending_materialized_cell_count() == 0,
        "erase_cell saved-output reacquired post-noop save should not expose dirty materialized cells");
    check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
        "erase_cell saved-output reacquired post-noop save should not expose dirty materialized memory");
    check(reacquired_editor.pending_worksheet_edits().empty(),
        "erase_cell saved-output reacquired post-noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "erase_cell saved-output reacquired post-noop save should not queue replacement diagnostics");
    check(!reacquired_editor.last_edit_error().has_value(),
        "erase_cell saved-output reacquired post-noop save should keep diagnostics clear");
    const auto reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
    const std::string reacquired_post_noop_xml =
        reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reacquired_post_noop_xml, "erase-cell-final",
        "erase_cell saved-output reacquired post-noop save should persist the later overwrite");
    check_not_contains(reacquired_post_noop_xml, "placeholder-a1",
        "erase_cell saved-output reacquired post-noop save should not revive old A1 text");
    check_not_contains(reacquired_post_noop_xml, "placeholder-a2",
        "erase_cell saved-output reacquired post-noop save should keep erased A2 absent");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "erase_cell saved-output reacquired post-noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) == reacquired_second_noop_entries,
        "erase_cell saved-output reacquired post-noop save should leave the second noop output unchanged");
    check(reacquired_noop_entries == output_entries,
        "erase_cell saved-output reacquired post-noop save should leave the first noop output stable");
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "erase_cell saved-output reacquired post-noop save should leave the second noop output stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "erase_cell saved-output reacquired post-noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(
        reacquired_post_noop_output, "Data",
        "erase_cell saved-output reacquired post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 2,
                "erase_cell post-noop reopened output should keep remaining sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 2,
                "erase_cell post-noop reopened output should keep shrunk bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 2,
                "erase_cell post-noop reopened sparse_cells should expose two cells");
            if (cells.size() == 2) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "erase-cell-final",
                    "erase_cell post-noop reopened sparse_cells should expose overwritten A1 first");
                check(cells[1].reference.row == 1 &&
                        cells[1].reference.column == 2 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[1].value.number_value() == 1.0,
                    "erase_cell post-noop reopened sparse_cells should expose source-backed B1 second");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "erase-cell-final" &&
                    row_one[1].reference.row == 1 &&
                    row_one[1].reference.column == 2 &&
                    row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    row_one[1].value.number_value() == 1.0,
                "erase_cell post-noop reopened row_cells should expose overwritten A1 and source B1");
            check(reopened_sheet.row_cells(2).empty(),
                "erase_cell post-noop reopened row_cells should keep erased row two absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 1 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[0].value.text_value() == "erase-cell-final",
                "erase_cell post-noop reopened column_cells should expose overwritten A1 only");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 1 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_two[0].value.number_value() == 1.0,
                "erase_cell post-noop reopened column_cells should expose source-backed B1 only");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "erase-cell-final",
                "erase_cell post-noop reopened output should read overwritten A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "erase_cell post-noop reopened output should keep source-backed B1");
            check(!reopened_sheet.try_cell("A2").has_value(),
                "erase_cell post-noop reopened output should keep erased A2 absent");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_erase_cell_saved_snapshot = [&](std::string_view scenario) {
        const std::string prefix(scenario);

        check(sheet.cell_count() == 2,
            prefix + " should keep the remaining represented sparse count");
        const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
            sheet.sparse_cells();
        check(cells.size() == 2,
            prefix + " should expose two full sparse snapshots");
        if (cells.size() == 2) {
            check(cells[0].reference.row == 1 &&
                    cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[0].value.text_value() == "placeholder-a1",
                prefix + " should keep source-backed A1 first");
            check(cells[1].reference.row == 1 &&
                    cells[1].reference.column == 2 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    cells[1].value.number_value() == 1.0,
                prefix + " should keep source-backed B1 second");
        }

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
            sheet.row_cells(1);
        check(row_one.size() == 2 &&
                row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                row_one[0].value.text_value() == "placeholder-a1" &&
                row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 2 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            prefix + " should keep row-one source snapshots");
        check(sheet.row_cells(2).empty(),
            prefix + " should keep erased row-two snapshots absent");

        const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
            sheet.column_cells(1);
        check(column_one.size() == 1 &&
                column_one[0].reference.row == 1 &&
                column_one[0].reference.column == 1 &&
                column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                column_one[0].value.text_value() == "placeholder-a1",
            prefix + " should keep column-one A1 snapshot");
        const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
            sheet.column_cells(2);
        check(column_two.size() == 1 &&
                column_two[0].reference.row == 1 &&
                column_two[0].reference.column == 2 &&
                column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_two[0].value.number_value() == 1.0,
            prefix + " should keep column-two B1 snapshot");

        check(!sheet.try_cell("A2").has_value(),
            prefix + " should keep erased A2 absent");
        check_cell_range_equals(sheet.used_range(), 1, 1, 1, 2,
            prefix + " should keep saved A1:B1 bounds");
        check(!sheet.has_pending_changes(),
            prefix + " should keep the materialized handle clean");
        check(editor.pending_change_count() == pending_count_after_save,
            prefix + " should not add another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            prefix + " should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            prefix + " should keep dirty materialized cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            prefix + " should keep dirty materialized memory empty");
        check(editor.pending_worksheet_edits().empty(),
            prefix + " should keep dirty summaries empty");
        check_workbook_editor_no_replacement_diagnostics(
            editor, prefix + " should keep replacement diagnostics empty");
        check(!editor.last_edit_error().has_value(),
            prefix + " should keep diagnostics clear");
    };
    check_erase_cell_saved_snapshot("erase_cell saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "erase_cell no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "erase_cell no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "erase_cell no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "erase_cell no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "erase_cell no-op save should keep diagnostics clear");
    check_erase_cell_saved_snapshot("erase_cell no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "erase_cell no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "erase_cell no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "erase_cell no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "erase_cell no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(noop_output, "Data", "erase_cell no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_erase_cell_projection(
                reopened_sheet, "erase_cell no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "erase_cell second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "erase_cell second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "erase_cell second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "erase_cell second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "erase_cell second no-op save should keep diagnostics clear");
    check_erase_cell_saved_snapshot("erase_cell second no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "erase_cell second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "erase_cell second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "erase_cell second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "erase_cell second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "erase_cell second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(second_noop_output, "Data", "erase_cell second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_erase_cell_projection(
                reopened_sheet, "erase_cell second no-op save");
        });
}

void test_public_worksheet_editor_erase_cell_removes_styled_source_record()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cell-style-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cell-style-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cell-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cell-style-second-noop-output.xlsx");
    const std::filesystem::path reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cell-style-reacquired-noop-output.xlsx");
    const std::filesystem::path reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cell-style-reacquired-second-noop-output.xlsx");
    const std::filesystem::path reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cell-style-reacquired-post-noop-output.xlsx");
    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        {
            fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
            non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
            styled_sheet.append_row({
                fastxlsx::CellView::number(1.0).with_style(non_default_style),
                fastxlsx::CellView::text("erase-cell-non-target"),
            });
        }
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");
    sheet.erase_cell("A1");

    check(sheet.cell_count() == 1,
        "styled erase_cell should remove one target sparse record");
    check(!sheet.try_cell("A1").has_value(),
        "styled erase_cell should remove the styled source cell");
    const fastxlsx::CellValue live_b1 = sheet.get_cell("B1");
    check(live_b1.kind() == fastxlsx::CellValueKind::Text &&
            live_b1.text_value() == "erase-cell-non-target" &&
            !live_b1.has_style(),
        "styled erase_cell should preserve non-target source cells without style leakage");
    check(editor.pending_materialized_cell_count() == 1,
        "styled erase_cell should update aggregate materialized diagnostics");
    check(sheet.has_pending_changes(),
        "styled erase_cell should dirty the materialized worksheet");
    check(!editor.last_edit_error().has_value(),
        "successful styled erase_cell should keep diagnostics clear");

    const auto inspect_styled_erase_cell_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 1,
                prefix + " reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 1,
                prefix + " reopened sparse_cells should expose B1 only");
            if (cells.size() == 1) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 2 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "erase-cell-non-target" &&
                        !cells[0].value.has_style(),
                    prefix + " reopened sparse_cells should keep non-target B1 only");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 1 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 2 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "erase-cell-non-target" &&
                    !row_one[0].value.has_style(),
                prefix + " reopened row_cells should expose non-target B1 only");
            check(reopened_sheet.column_cells(1).empty(),
                prefix + " reopened column_cells should keep erased A1 absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 1 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_two[0].value.text_value() == "erase-cell-non-target" &&
                    !column_two[0].value.has_style(),
                prefix + " reopened column_cells should expose non-target B1 only");

            check_cell_range_equals(reopened_sheet.used_range(), 1, 2, 1, 2,
                prefix + " reopened output should shrink to non-target cell");
            check(!reopened_sheet.try_cell("A1").has_value(),
                prefix + " reopened output should keep erased styled cell absent");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b1.text_value() == "erase-cell-non-target" &&
                    !reopened_b1.has_style(),
                prefix + " reopened output should keep non-target source cell unstyled");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled erase_cell save should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="B1"/>)",
        "styled erase_cell should shrink the projected dimension to the non-target cell");
    check_contains(worksheet_xml, "erase-cell-non-target",
        "styled erase_cell should persist non-target source cells");
    check_not_contains(worksheet_xml, R"(r="A1")",
        "styled erase_cell should omit erased styled source cell");
    check_not_contains(worksheet_xml,
        R"(s=")" + std::to_string(non_default_style.value()) + R"(")",
        "styled erase_cell should not leak removed source style ids into sheet data");
    check_reopened_clean_sheet_output(output, "Styled", "styled erase_cell",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cell_output(reopened_sheet, "styled erase_cell");
        });

    fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reacquired_sheet =
        reacquired_editor.worksheet("Styled");
    const auto check_reacquired_editor_clean_state =
        [&](std::string_view scenario) {
            const std::string prefix(scenario);

            check(!reacquired_editor.last_edit_error().has_value(),
                prefix + " should not expose stale diagnostics");
            check(!reacquired_editor.has_pending_changes(),
                prefix + " should keep editor state clean");
            check(reacquired_editor.pending_change_count() == 0 &&
                    reacquired_editor.pending_materialized_cell_count() == 0 &&
                    reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
                    reacquired_editor.pending_replacement_cell_count() == 0 &&
                    reacquired_editor.estimated_pending_replacement_memory_usage() == 0 &&
                    reacquired_editor.pending_worksheet_edits().empty(),
                prefix + " should not expose dirty diagnostics");
            check(reacquired_editor.pending_materialized_worksheet_names().empty() &&
                    reacquired_editor.pending_replacement_worksheet_names().empty(),
                prefix + " should not expose dirty worksheet names");
        };
    check_reacquired_editor_clean_state(
        "styled erase_cell saved-output reacquire");
    check(!reacquired_sheet.has_pending_changes(),
        "styled erase_cell saved-output reacquire should keep the sheet clean");
    inspect_styled_erase_cell_output(
        reacquired_sheet, "styled erase_cell saved-output reacquire");

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_noop,
        "styled erase_cell saved-output reacquired noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_noop,
        "styled erase_cell saved-output reacquired noop save");
    check_reacquired_editor_clean_state(
        "styled erase_cell saved-output reacquired noop save");
    check(!reacquired_sheet.has_pending_changes(),
        "styled erase_cell saved-output reacquired noop save should keep the sheet clean");
    inspect_styled_erase_cell_output(
        reacquired_sheet, "styled erase_cell saved-output reacquired noop save");
    const auto reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_noop_output);
    check(reacquired_noop_entries == output_entries,
        "styled erase_cell saved-output reacquired noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "styled erase_cell saved-output reacquired noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(
        reacquired_noop_output, "Styled",
        "styled erase_cell saved-output reacquired noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cell_output(
                reopened_sheet,
                "styled erase_cell saved-output reacquired noop save");
        });

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_second_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_second_noop,
        "styled erase_cell saved-output reacquired second noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_second_noop,
        "styled erase_cell saved-output reacquired second noop save");
    check_reacquired_editor_clean_state(
        "styled erase_cell saved-output reacquired second noop save");
    check(!reacquired_sheet.has_pending_changes(),
        "styled erase_cell saved-output reacquired second noop save should keep the sheet clean");
    inspect_styled_erase_cell_output(
        reacquired_sheet, "styled erase_cell saved-output reacquired second noop save");
    const auto reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_second_noop_output);
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "styled erase_cell saved-output reacquired second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "styled erase_cell saved-output reacquired second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "styled erase_cell saved-output reacquired second noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(
        reacquired_second_noop_output, "Styled",
        "styled erase_cell saved-output reacquired second noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cell_output(
                reopened_sheet,
                "styled erase_cell saved-output reacquired second noop save");
        });

    reacquired_sheet.set_cell("B1",
        fastxlsx::CellValue::text("styled-erase-final"));
    check(!reacquired_editor.last_edit_error().has_value(),
        "styled erase_cell saved-output reacquired post-noop edit should keep diagnostics clear");
    check(reacquired_sheet.has_pending_changes(),
        "styled erase_cell saved-output reacquired post-noop edit should dirty the sheet");
    check(reacquired_editor.has_pending_changes(),
        "styled erase_cell saved-output reacquired post-noop edit should dirty the editor");
    check(reacquired_sheet.cell_count() == 1,
        "styled erase_cell saved-output reacquired post-noop edit should keep sparse count stable");
    check(!reacquired_sheet.try_cell("A1").has_value(),
        "styled erase_cell saved-output reacquired post-noop edit should keep erased styled A1 absent");
    const fastxlsx::CellValue reacquired_final_b1 =
        reacquired_sheet.get_cell("B1");
    check(reacquired_final_b1.kind() == fastxlsx::CellValueKind::Text &&
            reacquired_final_b1.text_value() == "styled-erase-final" &&
            !reacquired_final_b1.has_style(),
        "styled erase_cell saved-output reacquired post-noop edit should overwrite unstyled B1");
    check_public_state_single_named_dirty_materialized_summary(
        reacquired_editor,
        reacquired_sheet,
        "Styled",
        0,
        "styled erase_cell saved-output reacquired post-noop edit");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "styled erase_cell saved-output reacquired post-noop edit should not queue replacement diagnostics");

    reacquired_editor.save_as(reacquired_post_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "styled erase_cell saved-output reacquired post-noop save should clean the sheet");
    check(reacquired_editor.pending_change_count() == 1,
        "styled erase_cell saved-output reacquired post-noop save should keep one materialized handoff");
    check(reacquired_editor.pending_materialized_worksheet_names().empty(),
        "styled erase_cell saved-output reacquired post-noop save should not expose dirty worksheet names");
    check(reacquired_editor.pending_materialized_cell_count() == 0,
        "styled erase_cell saved-output reacquired post-noop save should not expose dirty materialized cells");
    check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
        "styled erase_cell saved-output reacquired post-noop save should not expose dirty materialized memory");
    check(reacquired_editor.pending_worksheet_edits().empty(),
        "styled erase_cell saved-output reacquired post-noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "styled erase_cell saved-output reacquired post-noop save should not queue replacement diagnostics");
    check(!reacquired_editor.last_edit_error().has_value(),
        "styled erase_cell saved-output reacquired post-noop save should keep diagnostics clear");
    const auto reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
    const std::string reacquired_post_noop_xml =
        reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reacquired_post_noop_xml, R"(<dimension ref="B1"/>)",
        "styled erase_cell saved-output reacquired post-noop save should keep B1 bounds");
    check_contains(reacquired_post_noop_xml, "styled-erase-final",
        "styled erase_cell saved-output reacquired post-noop save should persist the later overwrite");
    check_not_contains(reacquired_post_noop_xml, "erase-cell-non-target",
        "styled erase_cell saved-output reacquired post-noop save should not keep the old B1 text");
    check_not_contains(reacquired_post_noop_xml, R"(r="A1")",
        "styled erase_cell saved-output reacquired post-noop save should keep erased styled A1 absent");
    check_not_contains(reacquired_post_noop_xml,
        R"(s=")" + std::to_string(non_default_style.value()) + R"(")",
        "styled erase_cell saved-output reacquired post-noop save should not leak removed style ids");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "styled erase_cell saved-output reacquired post-noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) == reacquired_second_noop_entries,
        "styled erase_cell saved-output reacquired post-noop save should leave the second noop output unchanged");
    check(reacquired_noop_entries == output_entries,
        "styled erase_cell saved-output reacquired post-noop save should leave the first noop output stable");
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "styled erase_cell saved-output reacquired post-noop save should leave the second noop output stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "styled erase_cell saved-output reacquired post-noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(
        reacquired_post_noop_output, "Styled",
        "styled erase_cell saved-output reacquired post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 1,
                "styled erase_cell post-noop reopened output should keep remaining sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 2, 1, 2,
                "styled erase_cell post-noop reopened output should keep B1 bounds");
            check(!reopened_sheet.try_cell("A1").has_value(),
                "styled erase_cell post-noop reopened output should keep erased styled A1 absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 1,
                "styled erase_cell post-noop reopened sparse_cells should expose B1 only");
            if (cells.size() == 1) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 2 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "styled-erase-final" &&
                        !cells[0].value.has_style(),
                    "styled erase_cell post-noop reopened sparse_cells should expose unstyled B1");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 1 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 2 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "styled-erase-final" &&
                    !row_one[0].value.has_style(),
                "styled erase_cell post-noop reopened row_cells should expose unstyled B1");
            check(reopened_sheet.column_cells(1).empty(),
                "styled erase_cell post-noop reopened column_cells should keep erased A1 absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 1 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_two[0].value.text_value() == "styled-erase-final" &&
                    !column_two[0].value.has_style(),
                "styled erase_cell post-noop reopened column_cells should expose unstyled B1");
            const fastxlsx::CellValue reopened_b1 =
                reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b1.text_value() == "styled-erase-final" &&
                    !reopened_b1.has_style(),
                "styled erase_cell post-noop reopened output should read overwritten unstyled B1");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_styled_erase_cell_saved_snapshot =
        [&](std::size_t expected_pending_count, std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 1,
                prefix + " should keep the represented sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 1,
                prefix + " should expose one represented record");
            if (cells.size() == 1) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 2 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "erase-cell-non-target" &&
                        !cells[0].value.has_style(),
                    prefix + " should keep non-target B1 only");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                sheet.row_cells(1);
            check(row_one.size() == 1 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 2 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "erase-cell-non-target" &&
                    !row_one[0].value.has_style(),
                prefix + " should keep row-one B1 snapshot");
            check(sheet.column_cells(1).empty(),
                prefix + " should keep erased column-one snapshots absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                sheet.column_cells(2);
            check(column_two.size() == 1 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_two[0].value.text_value() == "erase-cell-non-target" &&
                    !column_two[0].value.has_style(),
                prefix + " should keep column-two B1 snapshot");

            check(!sheet.try_cell("A1").has_value(),
                prefix + " should keep erased styled cell absent");
            check_cell_range_equals(sheet.used_range(), 1, 2, 1, 2,
                prefix + " should keep the saved B1 bounds");
            check(!sheet.has_pending_changes(),
                prefix + " should keep the materialized handle clean");
            check(editor.pending_change_count() == expected_pending_count,
                prefix + " should not add another materialized handoff");
            check(editor.pending_materialized_worksheet_names().empty(),
                prefix + " should keep dirty materialized names empty");
            check(editor.pending_materialized_cell_count() == 0,
                prefix + " should keep dirty materialized cells empty");
            check(editor.estimated_pending_materialized_memory_usage() == 0,
                prefix + " should keep dirty materialized memory empty");
            check(editor.pending_worksheet_edits().empty(),
                prefix + " should keep dirty summaries empty");
            check_workbook_editor_no_replacement_diagnostics(
                editor, prefix + " should keep replacement diagnostics empty");
            check(!editor.last_edit_error().has_value(),
                prefix + " should keep diagnostics clear");
        };
    check_styled_erase_cell_saved_snapshot(
        pending_count_after_save, "styled erase_cell saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "styled erase_cell no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "styled erase_cell no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "styled erase_cell no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "styled erase_cell no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "styled erase_cell no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "styled erase_cell no-op save should keep diagnostics clear");
    check_styled_erase_cell_saved_snapshot(
        pending_count_after_save, "styled erase_cell no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "styled erase_cell no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "styled erase_cell no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "styled erase_cell no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled erase_cell no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "styled erase_cell no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cell_output(
                reopened_sheet, "styled erase_cell no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "styled erase_cell second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "styled erase_cell second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "styled erase_cell second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "styled erase_cell second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "styled erase_cell second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "styled erase_cell second no-op save should keep diagnostics clear");
    check_styled_erase_cell_saved_snapshot(
        pending_count_after_save, "styled erase_cell second no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "styled erase_cell second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "styled erase_cell second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "styled erase_cell second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "styled erase_cell second no-op save should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "styled erase_cell second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled erase_cell second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled", "styled erase_cell second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cell_output(
                reopened_sheet, "styled erase_cell second no-op save");
        });
}

void test_public_worksheet_editor_erase_cells_remove_styled_source_records()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cells-style-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cells-style-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cells-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cells-style-second-noop-output.xlsx");
    const std::filesystem::path reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cells-style-reacquired-noop-output.xlsx");
    const std::filesystem::path reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cells-style-reacquired-second-noop-output.xlsx");
    const std::filesystem::path reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-cells-style-reacquired-post-noop-output.xlsx");
    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        {
            fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
            non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
            styled_sheet.append_row({
                fastxlsx::CellView::number(1.0).with_style(non_default_style),
                fastxlsx::CellView::text("erase-cell-range-target"),
                fastxlsx::CellView::text("cell-range-non-target"),
            });
        }
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");
    sheet.erase_cells(fastxlsx::CellRange {1, 1, 1, 2});

    check(sheet.cell_count() == 1,
        "styled erase_cells should remove all target sparse records");
    check(!sheet.try_cell("A1").has_value() &&
            !sheet.try_cell("B1").has_value(),
        "styled erase_cells should remove styled and unstyled target cells");
    check(sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 2}).empty(),
        "styled erase_cells range snapshot should keep erased target cells absent");
    const fastxlsx::CellValue live_c1 = sheet.get_cell("C1");
    check(live_c1.kind() == fastxlsx::CellValueKind::Text &&
            live_c1.text_value() == "cell-range-non-target" &&
            !live_c1.has_style(),
        "styled erase_cells should preserve non-target source cells without style leakage");
    check(editor.pending_materialized_cell_count() == 1,
        "styled erase_cells should update aggregate materialized diagnostics");
    check(sheet.has_pending_changes(),
        "styled erase_cells should dirty the materialized worksheet");
    check(!editor.last_edit_error().has_value(),
        "successful styled erase_cells should keep diagnostics clear");

    const auto inspect_styled_erase_cells_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 1,
                prefix + " reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 1,
                prefix + " reopened sparse_cells should expose C1 only");
            if (cells.size() == 1) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 3 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "cell-range-non-target" &&
                        !cells[0].value.has_style(),
                    prefix + " reopened sparse_cells should keep non-target C1 only");
            }
            check(reopened_sheet.sparse_cells(
                    fastxlsx::CellRange {1, 1, 1, 2}).empty(),
                prefix + " reopened range sparse_cells should keep erased cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 1 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 3 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "cell-range-non-target" &&
                    !row_one[0].value.has_style(),
                prefix + " reopened row_cells should expose non-target C1 only");

            check(reopened_sheet.column_cells(1).empty() &&
                    reopened_sheet.column_cells(2).empty(),
                prefix + " reopened column_cells should keep erased columns empty");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 1 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[0].value.text_value() == "cell-range-non-target" &&
                    !column_three[0].value.has_style(),
                prefix + " reopened column_cells should expose non-target C1 only");

            check_cell_range_equals(reopened_sheet.used_range(), 1, 3, 1, 3,
                prefix + " reopened output should shrink to non-target cell");
            check(!reopened_sheet.try_cell("A1").has_value() &&
                    !reopened_sheet.try_cell("B1").has_value(),
                prefix + " reopened output should keep erased target cells absent");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c1.text_value() == "cell-range-non-target" &&
                    !reopened_c1.has_style(),
                prefix + " reopened output should keep non-target source cell unstyled");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled erase_cells save should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="C1"/>)",
        "styled erase_cells should shrink the projected dimension to the non-target cell");
    check_contains(worksheet_xml, "cell-range-non-target",
        "styled erase_cells should persist non-target source cells");
    check_not_contains(worksheet_xml, R"(r="A1")",
        "styled erase_cells should omit erased styled source cell");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "styled erase_cells should omit erased unstyled source cell");
    check_not_contains(worksheet_xml,
        R"(s=")" + std::to_string(non_default_style.value()) + R"(")",
        "styled erase_cells should not leak removed source style ids into sheet data");
    check_reopened_clean_sheet_output(output, "Styled", "styled erase_cells",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cells_output(reopened_sheet, "styled erase_cells");
        });

    fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reacquired_sheet =
        reacquired_editor.worksheet("Styled");
    const auto check_reacquired_editor_clean_state =
        [&](std::string_view scenario) {
            const std::string prefix(scenario);

            check(!reacquired_editor.last_edit_error().has_value(),
                prefix + " should not expose stale diagnostics");
            check(!reacquired_editor.has_pending_changes(),
                prefix + " should keep editor state clean");
            check(reacquired_editor.pending_change_count() == 0 &&
                    reacquired_editor.pending_materialized_cell_count() == 0 &&
                    reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
                    reacquired_editor.pending_replacement_cell_count() == 0 &&
                    reacquired_editor.estimated_pending_replacement_memory_usage() == 0 &&
                    reacquired_editor.pending_worksheet_edits().empty(),
                prefix + " should not expose dirty diagnostics");
            check(reacquired_editor.pending_materialized_worksheet_names().empty() &&
                    reacquired_editor.pending_replacement_worksheet_names().empty(),
                prefix + " should not expose dirty worksheet names");
        };
    check_reacquired_editor_clean_state(
        "styled erase_cells saved-output reacquire");
    check(!reacquired_sheet.has_pending_changes(),
        "styled erase_cells saved-output reacquire should keep the sheet clean");
    inspect_styled_erase_cells_output(
        reacquired_sheet, "styled erase_cells saved-output reacquire");

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_noop,
        "styled erase_cells saved-output reacquired noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_noop,
        "styled erase_cells saved-output reacquired noop save");
    check_reacquired_editor_clean_state(
        "styled erase_cells saved-output reacquired noop save");
    check(!reacquired_sheet.has_pending_changes(),
        "styled erase_cells saved-output reacquired noop save should keep the sheet clean");
    inspect_styled_erase_cells_output(
        reacquired_sheet, "styled erase_cells saved-output reacquired noop save");
    const auto reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_noop_output);
    check(reacquired_noop_entries == output_entries,
        "styled erase_cells saved-output reacquired noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "styled erase_cells saved-output reacquired noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(
        reacquired_noop_output, "Styled",
        "styled erase_cells saved-output reacquired noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cells_output(
                reopened_sheet,
                "styled erase_cells saved-output reacquired noop save");
        });

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_second_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_second_noop,
        "styled erase_cells saved-output reacquired second noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_second_noop,
        "styled erase_cells saved-output reacquired second noop save");
    check_reacquired_editor_clean_state(
        "styled erase_cells saved-output reacquired second noop save");
    check(!reacquired_sheet.has_pending_changes(),
        "styled erase_cells saved-output reacquired second noop save should keep the sheet clean");
    inspect_styled_erase_cells_output(
        reacquired_sheet, "styled erase_cells saved-output reacquired second noop save");
    const auto reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_second_noop_output);
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "styled erase_cells saved-output reacquired second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "styled erase_cells saved-output reacquired second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "styled erase_cells saved-output reacquired second noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(
        reacquired_second_noop_output, "Styled",
        "styled erase_cells saved-output reacquired second noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cells_output(
                reopened_sheet,
                "styled erase_cells saved-output reacquired second noop save");
        });

    reacquired_sheet.set_cell("C1",
        fastxlsx::CellValue::text("styled-erase-cells-final"));
    check(!reacquired_editor.last_edit_error().has_value(),
        "styled erase_cells saved-output reacquired post-noop edit should keep diagnostics clear");
    check(reacquired_sheet.has_pending_changes(),
        "styled erase_cells saved-output reacquired post-noop edit should dirty the sheet");
    check(reacquired_editor.has_pending_changes(),
        "styled erase_cells saved-output reacquired post-noop edit should dirty the editor");
    check(reacquired_sheet.cell_count() == 1,
        "styled erase_cells saved-output reacquired post-noop edit should keep sparse count stable");
    check(!reacquired_sheet.try_cell("A1").has_value() &&
            !reacquired_sheet.try_cell("B1").has_value(),
        "styled erase_cells saved-output reacquired post-noop edit should keep erased targets absent");
    check(reacquired_sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 2}).empty(),
        "styled erase_cells saved-output reacquired post-noop edit should keep erased range snapshots absent");
    const fastxlsx::CellValue reacquired_final_c1 =
        reacquired_sheet.get_cell("C1");
    check(reacquired_final_c1.kind() == fastxlsx::CellValueKind::Text &&
            reacquired_final_c1.text_value() == "styled-erase-cells-final" &&
            !reacquired_final_c1.has_style(),
        "styled erase_cells saved-output reacquired post-noop edit should overwrite unstyled C1");
    check_public_state_single_named_dirty_materialized_summary(
        reacquired_editor,
        reacquired_sheet,
        "Styled",
        0,
        "styled erase_cells saved-output reacquired post-noop edit");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "styled erase_cells saved-output reacquired post-noop edit should not queue replacement diagnostics");

    reacquired_editor.save_as(reacquired_post_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "styled erase_cells saved-output reacquired post-noop save should clean the sheet");
    check(reacquired_editor.pending_change_count() == 1,
        "styled erase_cells saved-output reacquired post-noop save should keep one materialized handoff");
    check(reacquired_editor.pending_materialized_worksheet_names().empty(),
        "styled erase_cells saved-output reacquired post-noop save should not expose dirty worksheet names");
    check(reacquired_editor.pending_materialized_cell_count() == 0,
        "styled erase_cells saved-output reacquired post-noop save should not expose dirty materialized cells");
    check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
        "styled erase_cells saved-output reacquired post-noop save should not expose dirty materialized memory");
    check(reacquired_editor.pending_worksheet_edits().empty(),
        "styled erase_cells saved-output reacquired post-noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "styled erase_cells saved-output reacquired post-noop save should not queue replacement diagnostics");
    check(!reacquired_editor.last_edit_error().has_value(),
        "styled erase_cells saved-output reacquired post-noop save should keep diagnostics clear");
    const auto reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
    const std::string reacquired_post_noop_xml =
        reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reacquired_post_noop_xml, R"(<dimension ref="C1"/>)",
        "styled erase_cells saved-output reacquired post-noop save should keep C1 bounds");
    check_contains(reacquired_post_noop_xml, "styled-erase-cells-final",
        "styled erase_cells saved-output reacquired post-noop save should persist the later overwrite");
    check_not_contains(reacquired_post_noop_xml, "cell-range-non-target",
        "styled erase_cells saved-output reacquired post-noop save should not keep the old C1 text");
    check_not_contains(reacquired_post_noop_xml, R"(r="A1")",
        "styled erase_cells saved-output reacquired post-noop save should keep erased styled A1 absent");
    check_not_contains(reacquired_post_noop_xml, R"(r="B1")",
        "styled erase_cells saved-output reacquired post-noop save should keep erased unstyled B1 absent");
    check_not_contains(reacquired_post_noop_xml,
        R"(s=")" + std::to_string(non_default_style.value()) + R"(")",
        "styled erase_cells saved-output reacquired post-noop save should not leak removed style ids");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "styled erase_cells saved-output reacquired post-noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) == reacquired_second_noop_entries,
        "styled erase_cells saved-output reacquired post-noop save should leave the second noop output unchanged");
    check(reacquired_noop_entries == output_entries,
        "styled erase_cells saved-output reacquired post-noop save should leave the first noop output stable");
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "styled erase_cells saved-output reacquired post-noop save should leave the second noop output stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "styled erase_cells saved-output reacquired post-noop save should leave the saved input unchanged");
    check_reopened_clean_sheet_output(
        reacquired_post_noop_output, "Styled",
        "styled erase_cells saved-output reacquired post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 1,
                "styled erase_cells post-noop reopened output should keep remaining sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 3, 1, 3,
                "styled erase_cells post-noop reopened output should keep C1 bounds");
            check(!reopened_sheet.try_cell("A1").has_value() &&
                    !reopened_sheet.try_cell("B1").has_value(),
                "styled erase_cells post-noop reopened output should keep erased targets absent");
            check(reopened_sheet.sparse_cells(
                    fastxlsx::CellRange {1, 1, 1, 2}).empty(),
                "styled erase_cells post-noop reopened output should keep erased range snapshots absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 1,
                "styled erase_cells post-noop reopened sparse_cells should expose C1 only");
            if (cells.size() == 1) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 3 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "styled-erase-cells-final" &&
                        !cells[0].value.has_style(),
                    "styled erase_cells post-noop reopened sparse_cells should expose unstyled C1");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 1 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 3 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "styled-erase-cells-final" &&
                    !row_one[0].value.has_style(),
                "styled erase_cells post-noop reopened row_cells should expose unstyled C1");
            check(reopened_sheet.column_cells(1).empty() &&
                    reopened_sheet.column_cells(2).empty(),
                "styled erase_cells post-noop reopened column_cells should keep erased columns absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 1 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[0].value.text_value() == "styled-erase-cells-final" &&
                    !column_three[0].value.has_style(),
                "styled erase_cells post-noop reopened column_cells should expose unstyled C1");
            const fastxlsx::CellValue reopened_c1 =
                reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c1.text_value() == "styled-erase-cells-final" &&
                    !reopened_c1.has_style(),
                "styled erase_cells post-noop reopened output should read overwritten unstyled C1");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_styled_erase_cells_saved_snapshot =
        [&](std::size_t expected_pending_count, std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 1,
                prefix + " should keep the represented sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 1,
                prefix + " should expose one represented record");
            if (cells.size() == 1) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 3 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "cell-range-non-target" &&
                        !cells[0].value.has_style(),
                    prefix + " should keep non-target C1 only");
            }
            check(sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 2}).empty(),
                prefix + " should keep erased range snapshots absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                sheet.row_cells(1);
            check(row_one.size() == 1 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 3 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "cell-range-non-target" &&
                    !row_one[0].value.has_style(),
                prefix + " should keep row-one C1 snapshot");

            check(sheet.column_cells(1).empty() && sheet.column_cells(2).empty(),
                prefix + " should keep erased column snapshots absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                sheet.column_cells(3);
            check(column_three.size() == 1 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[0].value.text_value() == "cell-range-non-target" &&
                    !column_three[0].value.has_style(),
                prefix + " should keep column-three C1 snapshot");

            check(!sheet.try_cell("A1").has_value() &&
                    !sheet.try_cell("B1").has_value(),
                prefix + " should keep erased target cells absent");
            check_cell_range_equals(sheet.used_range(), 1, 3, 1, 3,
                prefix + " should keep the saved C1 bounds");
            check(!sheet.has_pending_changes(),
                prefix + " should keep the materialized handle clean");
            check(editor.pending_change_count() == expected_pending_count,
                prefix + " should not add another materialized handoff");
            check(editor.pending_materialized_worksheet_names().empty(),
                prefix + " should keep dirty materialized names empty");
            check(editor.pending_materialized_cell_count() == 0,
                prefix + " should keep dirty materialized cells empty");
            check(editor.estimated_pending_materialized_memory_usage() == 0,
                prefix + " should keep dirty materialized memory empty");
            check(editor.pending_worksheet_edits().empty(),
                prefix + " should keep dirty summaries empty");
            check_workbook_editor_no_replacement_diagnostics(
                editor, prefix + " should keep replacement diagnostics empty");
            check(!editor.last_edit_error().has_value(),
                prefix + " should keep diagnostics clear");
        };
    check_styled_erase_cells_saved_snapshot(
        pending_count_after_save, "styled erase_cells saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "styled erase_cells no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "styled erase_cells no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "styled erase_cells no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "styled erase_cells no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "styled erase_cells no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "styled erase_cells no-op save should keep diagnostics clear");
    check_styled_erase_cells_saved_snapshot(
        pending_count_after_save, "styled erase_cells no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "styled erase_cells no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "styled erase_cells no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "styled erase_cells no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled erase_cells no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "styled erase_cells no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cells_output(
                reopened_sheet, "styled erase_cells no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "styled erase_cells second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "styled erase_cells second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "styled erase_cells second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "styled erase_cells second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "styled erase_cells second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "styled erase_cells second no-op save should keep diagnostics clear");
    check_styled_erase_cells_saved_snapshot(
        pending_count_after_save, "styled erase_cells second no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "styled erase_cells second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "styled erase_cells second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "styled erase_cells second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "styled erase_cells second no-op save should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "styled erase_cells second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled erase_cells second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled", "styled erase_cells second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_erase_cells_output(
                reopened_sheet, "styled erase_cells second no-op save");
        });
}

void test_public_worksheet_editor_erase_cells_range_reacquires_saved_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-range-erase-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-range-erase-first.xlsx");
    const std::filesystem::path first_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-range-erase-first-noop.xlsx");
    const std::filesystem::path first_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-range-erase-first-second-noop.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-range-erase-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-range-erase-second-noop.xlsx");
    const std::filesystem::path reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-range-erase-reacquired-second-noop.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(sheet.cell_count() == 3,
        "public WorksheetEditor should materialize all represented source cells before range erase");
    sheet.erase_cells(fastxlsx::CellRange {1, 1, 2, 2});
    check(!sheet.try_cell("A1").has_value(),
        "public WorksheetEditor range erase should remove A1");
    check(!sheet.try_cell("B1").has_value(),
        "public WorksheetEditor range erase should remove B1");
    check(!sheet.try_cell("A2").has_value(),
        "public WorksheetEditor range erase should remove A2");
    check(sheet.cell_count() == 0,
        "public WorksheetEditor range erase should update sparse cell count");
    check(sheet.has_pending_changes(),
        "public WorksheetEditor range erase should dirty the materialized sheet");
    check(editor.has_pending_changes(),
        "public WorksheetEditor range erase should dirty the owning editor");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "range erase dirty summary");

    const auto check_reopened_range_erase_empty_projection =
        [](fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 0,
                prefix + " reopened output should stay empty");
            check(reopened_sheet.sparse_cells().empty(),
                prefix + " reopened sparse_cells should stay empty");
            check(reopened_sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2}).empty(),
                prefix + " reopened range sparse_cells should stay empty");
            check(reopened_sheet.row_cells(1).empty() &&
                    reopened_sheet.row_cells(2).empty(),
                prefix + " reopened row_cells should keep erased rows empty");
            check(reopened_sheet.column_cells(1).empty() &&
                    reopened_sheet.column_cells(2).empty(),
                prefix + " reopened column_cells should keep erased columns empty");
            check(!reopened_sheet.used_range().has_value(),
                prefix + " reopened output should expose no sparse bounds");
            check(!reopened_sheet.try_cell("A1").has_value(),
                prefix + " reopened output should keep erased A1 absent");
            check(!reopened_sheet.try_cell("B1").has_value(),
                prefix + " reopened output should keep erased B1 absent");
            check(!reopened_sheet.try_cell("A2").has_value(),
                prefix + " reopened output should keep erased A2 absent");
        };

    const auto check_reopened_range_erase_reacquired_projection =
        [](fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 1,
                prefix + " reopened output should contain one sparse cell");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 1,
                prefix + " reopened sparse_cells should expose C3 only");
            if (cells.size() == 1) {
                check(cells[0].reference.row == 3 &&
                        cells[0].reference.column == 3 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "range-erase-reacquired",
                    prefix + " reopened sparse_cells should keep C3 text");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 1 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 3 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "range-erase-reacquired",
                prefix + " reopened row_cells should expose C3 only");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 1 &&
                    column_three[0].reference.row == 3 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[0].value.text_value() == "range-erase-reacquired",
                prefix + " reopened column_cells should expose C3 only");

            check_cell_range_equals(reopened_sheet.used_range(), 3, 3, 3, 3,
                prefix + " reopened output should expose C3 bounds");
            check(!reopened_sheet.try_cell("A1").has_value(),
                prefix + " reopened output should keep erased A1 absent");
            check(!reopened_sheet.try_cell("B1").has_value(),
                prefix + " reopened output should keep erased B1 absent");
            check(!reopened_sheet.try_cell("A2").has_value(),
                prefix + " reopened output should keep erased A2 absent");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "range-erase-reacquired",
                prefix + " reopened output should read post-reacquire C3");
        };

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "successful save_as should clear range-erased materialized sheet dirty state");
    check(editor.pending_change_count() == 1,
        "range erase save_as should expose one materialized worksheet handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range erase first save should leave the source package unchanged");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1"/>)",
        "range erase of all represented cells should shrink the projected dimension to A1");
    check_not_contains(first_worksheet_xml, "placeholder-a1",
        "range erase save_as should omit erased A1 text");
    check_not_contains(first_worksheet_xml, "placeholder-a2",
        "range erase save_as should omit erased A2 text");
    check_not_contains(first_worksheet_xml, R"(r="B1")",
        "range erase save_as should omit erased B1 numeric cell");
    check_contains(first_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "range erase save_as should preserve untouched worksheets");
    check_reopened_clean_sheet_output(first_output, "Data", "range erase first save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_range_erase_empty_projection(
                reopened_sheet, "range erase first save");
        });

    const std::size_t pending_count_after_first_save = editor.pending_change_count();
    const auto check_range_erase_empty_saved_snapshot =
        [&](std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 0,
                prefix + " should keep the saved worksheet empty");
            check(sheet.sparse_cells().empty(),
                prefix + " should expose no full sparse snapshots");
            check(sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2}).empty(),
                prefix + " should expose no erased range snapshots");
            check(sheet.row_cells(1).empty() && sheet.row_cells(2).empty(),
                prefix + " should keep erased row snapshots absent");
            check(sheet.column_cells(1).empty() && sheet.column_cells(2).empty(),
                prefix + " should keep erased column snapshots absent");
            check(!sheet.try_cell("A1").has_value() &&
                    !sheet.try_cell("B1").has_value() &&
                    !sheet.try_cell("A2").has_value(),
                prefix + " should keep erased source cells absent");
            check(!sheet.used_range().has_value(),
                prefix + " should keep sparse bounds empty");
            check(!sheet.has_pending_changes(),
                prefix + " should keep the materialized handle clean");
            check(editor.pending_change_count() == pending_count_after_first_save,
                prefix + " should not add another materialized handoff");
            check(editor.pending_materialized_worksheet_names().empty(),
                prefix + " should keep dirty materialized names empty");
            check(editor.pending_materialized_cell_count() == 0,
                prefix + " should keep dirty materialized cells empty");
            check(editor.estimated_pending_materialized_memory_usage() == 0,
                prefix + " should keep dirty materialized memory empty");
            check(editor.pending_worksheet_edits().empty(),
                prefix + " should keep dirty summaries empty");
            check_workbook_editor_no_replacement_diagnostics(
                editor, prefix + " should keep replacement diagnostics empty");
            check(!editor.last_edit_error().has_value(),
                prefix + " should keep diagnostics clear");
        };
    check_range_erase_empty_saved_snapshot("range erase first saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_first_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_first_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(first_noop_output);
    check(!sheet.has_pending_changes(),
        "range erase first no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "range erase first no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "range erase first no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "range erase first no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "range erase first no-op save should keep diagnostics clear");
    check_range_erase_empty_saved_snapshot("range erase first no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_first_noop,
        "range erase first no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_first_noop,
        "range erase first no-op save");
    const auto first_noop_entries = fastxlsx::test::read_zip_entries(first_noop_output);
    check(first_noop_entries == first_entries,
        "range erase first no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range erase first no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(first_noop_output, "Data", "range erase first no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_range_erase_empty_projection(
                reopened_sheet, "range erase first no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_first_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_first_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(first_second_noop_output);
    check(!sheet.has_pending_changes(),
        "range erase first second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "range erase first second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "range erase first second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "range erase first second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "range erase first second no-op save should keep diagnostics clear");
    check_range_erase_empty_saved_snapshot(
        "range erase first second no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_first_second_noop,
        "range erase first second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_first_second_noop,
        "range erase first second no-op save");
    check(fastxlsx::test::read_zip_entries(first_second_noop_output) == first_noop_entries,
        "range erase first second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(first_noop_output) == first_noop_entries,
        "range erase first second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range erase first second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(first_second_noop_output, "Data",
        "range erase first second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_range_erase_empty_projection(
                reopened_sheet, "range erase first second no-op save");
        });

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes(),
        "matching worksheet reacquire after range erase save_as should be clean");
    check(reacquired.cell_count() == 0,
        "matching worksheet reacquire should reuse the erased sparse state");
    check(!reacquired.try_cell("A1").has_value(),
        "matching worksheet reacquire should keep erased A1 missing");
    check(!reacquired.try_cell("B1").has_value(),
        "matching worksheet reacquire should keep erased B1 missing");
    check(!reacquired.try_cell("A2").has_value(),
        "matching worksheet reacquire should keep erased A2 missing");

    reacquired.erase_cells(fastxlsx::CellRange {1, 1, 2, 2});
    check(!reacquired.has_pending_changes(),
        "missing-only range erase after matching reacquire should remain a clean no-op");
    check(!editor.last_edit_error().has_value(),
        "missing-only range erase after matching reacquire should leave diagnostics clear");

    reacquired.set_cell(3, 3, fastxlsx::CellValue::text("range-erase-reacquired"));
    check(reacquired.has_pending_changes(),
        "post-reacquire mutation should dirty the reused materialized sheet");

    editor.save_as(second_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range erase reacquired save should leave the source package unchanged");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_worksheet_xml, "range-erase-reacquired",
        "post-reacquire mutation should persist on the second save_as");
    check_not_contains(second_worksheet_xml, "placeholder-a1",
        "erased A1 text should not reappear after post-reacquire mutation");
    check_not_contains(second_worksheet_xml, "placeholder-a2",
        "erased A2 text should not reappear after post-reacquire mutation");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "erased B1 numeric cell should not reappear after post-reacquire mutation");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "range erase reacquired no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "range erase reacquired no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "range erase reacquired no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "range erase reacquired no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "range erase reacquired no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "range erase reacquired no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "range erase reacquired no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "range erase reacquired no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range erase reacquired no-op save should leave the source package unchanged");

    const auto check_range_erase_reacquired_saved_snapshots =
        [&](fastxlsx::WorksheetEditor& handle, std::string_view prefix) {
            check(handle.cell_count() == 1,
                std::string(prefix) + " should keep one represented sparse cell");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                handle.sparse_cells();
            check(cells.size() == 1,
                std::string(prefix) + " should expose one row-major sparse snapshot");
            if (cells.size() == 1) {
                check(cells[0].reference.row == 3 &&
                        cells[0].reference.column == 3 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "range-erase-reacquired",
                    std::string(prefix) + " should keep the saved C3 text snapshot");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                handle.row_cells(3);
            check(row_three.size() == 1 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 3 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "range-erase-reacquired",
                std::string(prefix) + " should keep the row-three C3 snapshot");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                handle.column_cells(3);
            check(column_three.size() == 1 &&
                    column_three[0].reference.row == 3 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[0].value.text_value() == "range-erase-reacquired",
                std::string(prefix) + " should keep the column-three C3 snapshot");
            check(!handle.try_cell("A1").has_value() &&
                    !handle.try_cell("B1").has_value() &&
                    !handle.try_cell("A2").has_value(),
                std::string(prefix) + " should keep erased source cells absent");
            check_cell_range_equals(handle.used_range(), 3, 3, 3, 3,
                std::string(prefix) + " should keep the saved C3 bounds");
            check(!handle.has_pending_changes(),
                std::string(prefix) + " should keep the handle clean");
            check(editor.pending_change_count() == 2,
                std::string(prefix) + " should not add another materialized handoff");
            check(editor.pending_materialized_worksheet_names().empty(),
                std::string(prefix) + " should keep dirty materialized names empty");
            check(editor.pending_materialized_cell_count() == 0,
                std::string(prefix) + " should keep dirty materialized cells empty");
            check(editor.estimated_pending_materialized_memory_usage() == 0,
                std::string(prefix) + " should keep dirty materialized memory empty");
            check_workbook_editor_no_replacement_diagnostics(
                editor,
                std::string(prefix) +
                    " should keep replacement diagnostics empty");
            check(!editor.last_edit_error().has_value(),
                std::string(prefix) + " should keep diagnostics clear");
        };
    check_range_erase_reacquired_saved_snapshots(
        sheet,
        "range erase reacquired original saved handle");
    check_range_erase_reacquired_saved_snapshots(
        reacquired,
        "range erase reacquired matching saved handle");

    check_reopened_clean_sheet_output(second_output, "Data", "range erase reacquired save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_range_erase_reacquired_projection(
                reopened_sheet, "range erase reacquired save");
        });
    check_reopened_clean_sheet_output(noop_output, "Data", "range erase reacquired no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_range_erase_reacquired_projection(
                reopened_sheet, "range erase reacquired no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquired_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquired_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(reacquired_second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "range erase reacquired second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "range erase reacquired second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "range erase reacquired second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "range erase reacquired second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "range erase reacquired second no-op save should keep diagnostics clear");
    check_range_erase_reacquired_saved_snapshots(
        sheet,
        "range erase reacquired original second no-op saved handle");
    check_range_erase_reacquired_saved_snapshots(
        reacquired,
        "range erase reacquired matching second no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_reacquired_second_noop,
        "range erase reacquired second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_reacquired_second_noop,
        "range erase reacquired second no-op save");
    const auto reacquired_noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) ==
            reacquired_noop_entries,
        "range erase reacquired second no-op output should match the first reacquired no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == reacquired_noop_entries,
        "range erase reacquired second no-op save should leave the first reacquired no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range erase reacquired second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(reacquired_second_noop_output, "Data",
        "range erase reacquired second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_reopened_range_erase_reacquired_projection(
                reopened_sheet, "range erase reacquired second no-op save");
        });
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_erase_cell_auto_flushes_on_save_as();
        test_public_worksheet_editor_erase_cell_removes_styled_source_record();
        test_public_worksheet_editor_erase_cells_remove_styled_source_records();
        test_public_worksheet_editor_erase_cells_range_reacquires_saved_state();
        std::cout << "WorkbookEditor public-state erase cell tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state erase cell test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
