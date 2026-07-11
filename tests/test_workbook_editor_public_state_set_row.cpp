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

void test_public_worksheet_editor_set_row_replaces_sparse_row()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-row-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-row-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-row-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-row-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_row(1, {
        fastxlsx::CellValue::text("set-row-a1"),
        fastxlsx::CellValue::number(22.0),
        fastxlsx::CellValue::formula("A1+B2"),
        fastxlsx::CellValue::blank(),
        fastxlsx::CellValue::error("#N/A"),
    });

    check(sheet.cell_count() == 6,
        "set_row should replace the target sparse row without clearing other rows");
    check(sheet.get_cell("A1").text_value() == "set-row-a1",
        "set_row should write the first value to column A of the target row");
    check(sheet.get_cell("B1").number_value() == 22.0,
        "set_row should write numeric values by input order");
    const fastxlsx::CellValue formula = sheet.get_cell("C1");
    check(formula.kind() == fastxlsx::CellValueKind::Formula
            && formula.text_value() == "A1+B2",
        "set_row should preserve formula text as a formula cell");
    check(sheet.get_cell("D1").kind() == fastxlsx::CellValueKind::Blank,
        "set_row should represent explicit blank values");
    const fastxlsx::CellValue error = sheet.get_cell("E1");
    check(error.kind() == fastxlsx::CellValueKind::Error &&
            error.text_value() == "#N/A",
        "set_row should preserve error values as opaque error cells");
    check(sheet.get_cell("A2").text_value() == "placeholder-a2",
        "set_row should preserve represented cells outside the target row");
    check(!sheet.try_cell("B2").has_value(),
        "set_row should not synthesize cells outside the target row");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(sheet.has_pending_changes(),
        "set_row should dirty the materialized worksheet when values are replaced");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "set_row should report the dirty materialized worksheet name");
    check(editor.pending_materialized_cell_count() == 6,
        "set_row should contribute the replaced sparse records to aggregate diagnostics");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "set_row should contribute the replaced sparse records to aggregate memory diagnostics");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "set_row dirty summary");
    check(!editor.last_edit_error().has_value(),
        "successful set_row should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row save should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E2"/>)",
        "set_row should refresh the dirty worksheet dimension");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" t="inlineStr"><is><t>set-row-a1</t></is></c>)",
        "set_row should persist target-row text in column order");
    check_contains(worksheet_xml, R"(<c r="B1"><v>22</v></c>)",
        "set_row should persist target-row numeric cells");
    check_contains(worksheet_xml, R"(<c r="C1"><f>A1+B2</f></c>)",
        "set_row should persist target-row formula cells");
    check_contains(worksheet_xml, R"(<c r="D1"/>)",
        "set_row should persist explicit blank cells");
    check_contains(worksheet_xml, R"(<c r="E1" t="e"><v>#N/A</v></c>)",
        "set_row should persist target-row error cells");
    check_contains(worksheet_xml, "placeholder-a2",
        "set_row should keep non-target row source cells");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "set_row should omit the old target-row text");
    check_not_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "set_row should omit the old target-row numeric value");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "set_row should preserve untouched worksheets");
    const auto inspect_set_row_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "set_row reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
                "set_row reopened output should keep replaced row bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "set-row-a1",
                "set_row reopened output should read target-row text");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 22.0,
                "set_row reopened output should read target-row number");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c1.text_value() == "A1+B2",
                "set_row reopened output should read target-row formula");
            const fastxlsx::CellValue reopened_d1 = reopened_sheet.get_cell("D1");
            check(reopened_d1.kind() == fastxlsx::CellValueKind::Blank,
                "set_row reopened output should read target-row explicit blank");
            const fastxlsx::CellValue reopened_e1 = reopened_sheet.get_cell("E1");
            check(reopened_e1.kind() == fastxlsx::CellValueKind::Error &&
                    reopened_e1.text_value() == "#N/A",
                "set_row reopened output should read target-row error");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "placeholder-a2",
                "set_row reopened output should keep non-target rows");
            check(!reopened_sheet.try_cell("B2").has_value(),
                "set_row reopened output should not synthesize non-target row cells");
        };
    check_reopened_clean_sheet_output(output, "Data", "set_row",
        inspect_set_row_output);
    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_set_row_saved_snapshot =
        [&](std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 6,
                prefix + " should keep the saved sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 6,
                prefix + " should expose all saved sparse records");
            if (cells.size() == 6) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "set-row-a1",
                    prefix + " should keep A1 text first");
                check(cells[1].reference.row == 1 &&
                        cells[1].reference.column == 2 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[1].value.number_value() == 22.0,
                    prefix + " should keep B1 number second");
                check(cells[2].reference.row == 1 &&
                        cells[2].reference.column == 3 &&
                        cells[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                        cells[2].value.text_value() == "A1+B2",
                    prefix + " should keep C1 formula third");
                check(cells[3].reference.row == 1 &&
                        cells[3].reference.column == 4 &&
                        cells[3].value.kind() == fastxlsx::CellValueKind::Blank,
                    prefix + " should keep D1 blank fourth");
                check(cells[4].reference.row == 1 &&
                        cells[4].reference.column == 5 &&
                        cells[4].value.kind() == fastxlsx::CellValueKind::Error &&
                        cells[4].value.text_value() == "#N/A",
                    prefix + " should keep E1 error fifth");
                check(cells[5].reference.row == 2 &&
                        cells[5].reference.column == 1 &&
                        cells[5].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[5].value.text_value() == "placeholder-a2",
                    prefix + " should keep non-target A2 last");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                sheet.row_cells(1);
            check(row_one.size() == 5 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "set-row-a1" &&
                    row_one[1].reference.row == 1 &&
                    row_one[1].reference.column == 2 &&
                    row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    row_one[1].value.number_value() == 22.0 &&
                    row_one[2].reference.row == 1 &&
                    row_one[2].reference.column == 3 &&
                    row_one[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    row_one[2].value.text_value() == "A1+B2" &&
                    row_one[3].reference.row == 1 &&
                    row_one[3].reference.column == 4 &&
                    row_one[3].value.kind() == fastxlsx::CellValueKind::Blank &&
                    row_one[4].reference.row == 1 &&
                    row_one[4].reference.column == 5 &&
                    row_one[4].value.kind() == fastxlsx::CellValueKind::Error &&
                    row_one[4].value.text_value() == "#N/A",
                prefix + " should expose the replaced row in column order");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                sheet.row_cells(2);
            check(row_two.size() == 1 &&
                    row_two[0].reference.row == 2 &&
                    row_two[0].reference.column == 1 &&
                    row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_two[0].value.text_value() == "placeholder-a2",
                prefix + " should keep non-target row-two text");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                sheet.column_cells(1);
            check(column_one.size() == 2 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[0].value.text_value() == "set-row-a1" &&
                    column_one[1].reference.row == 2 &&
                    column_one[1].reference.column == 1 &&
                    column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[1].value.text_value() == "placeholder-a2",
                prefix + " should expose column-one records in row order");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                sheet.column_cells(2);
            check(column_two.size() == 1 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_two[0].value.number_value() == 22.0,
                prefix + " should expose column-two number");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                sheet.column_cells(3);
            check(column_three.size() == 1 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    column_three[0].value.text_value() == "A1+B2",
                prefix + " should expose column-three formula");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 1 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Blank,
                prefix + " should expose column-four blank");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_five =
                sheet.column_cells(5);
            check(column_five.size() == 1 &&
                    column_five[0].reference.row == 1 &&
                    column_five[0].reference.column == 5 &&
                    column_five[0].value.kind() == fastxlsx::CellValueKind::Error &&
                    column_five[0].value.text_value() == "#N/A",
                prefix + " should expose column-five error");

            check_cell_range_equals(sheet.used_range(), 1, 1, 2, 5,
                prefix + " should keep saved sparse bounds");
            check(!sheet.try_cell("B2").has_value(),
                prefix + " should not synthesize non-target cells");
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
    check_set_row_saved_snapshot("set_row saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_row no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "set_row no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row no-op save should keep diagnostics clear");
    check_set_row_saved_snapshot("set_row no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "set_row no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "set_row no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_row no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(noop_output, "Data", "set_row no-op save",
        inspect_set_row_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "set_row second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "set_row second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "set_row second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "set_row second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "set_row second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_row second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Data", "set_row second no-op save",
        inspect_set_row_output);
}

void test_public_worksheet_editor_set_row_replacement_drops_source_styles()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-full-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-full-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-full-style-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("set-row-full-target-tail"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::number(2.0).with_style(non_default_style),
            fastxlsx::CellView::text("set-row-full-non-target"),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_row(1, {
        fastxlsx::CellValue::text("row-replacement-unstyled"),
        fastxlsx::CellValue::number(42.0),
        fastxlsx::CellValue::formula("A1+B1"),
    });

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Text &&
            live_a1.text_value() == "row-replacement-unstyled" &&
            !live_a1.has_style(),
        "set_row full replacement should drop overwritten source style ids");
    const fastxlsx::CellValue live_a2 = sheet.get_cell("A2");
    check(live_a2.kind() == fastxlsx::CellValueKind::Number &&
            live_a2.number_value() == 2.0 &&
            live_a2.has_style() &&
            live_a2.style_id().value() == non_default_style.value(),
        "set_row full replacement should preserve non-target source style ids");
    const fastxlsx::CellValue live_c1 = sheet.get_cell("C1");
    check(live_c1.kind() == fastxlsx::CellValueKind::Formula &&
            live_c1.text_value() == "A1+B1" &&
            !live_c1.has_style(),
        "set_row full replacement should insert new row values without style ids");
    check(sheet.cell_count() == 5,
        "set_row full replacement should replace only the target-row sparse records");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "set_row full replacement should keep target and non-target bounds");
    check(sheet.has_pending_changes(),
        "set_row full replacement should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 5,
        "set_row full replacement should expose aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_row full replacement dirty summary");
    check(!editor.last_edit_error().has_value(),
        "successful set_row full replacement should keep diagnostics clear");

    const auto check_row_a1_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "row-replacement-unstyled" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement A1");
        };
    const auto check_row_b1_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 42.0 &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement B1");
        };
    const auto check_row_c1_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1+B1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement C1");
        };
    const auto check_row_a2_projection =
        [non_default_style](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 2.0 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                prefix + " should preserve non-target styled A2");
        };
    const auto check_row_b2_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-row-full-non-target" &&
                    !snapshot.value.has_style(),
                prefix + " should preserve non-target unstyled B2");
        };

    const auto inspect_set_row_replacement_output =
        [check_row_a1_projection, check_row_b1_projection, check_row_c1_projection,
            check_row_a2_projection, check_row_b2_projection](
            fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 5,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                prefix + " reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " reopened sparse_cells should expose five records");
            if (cells.size() == 5) {
                check_row_a1_projection(cells[0],
                    prefix + " reopened sparse_cells");
                check_row_b1_projection(cells[1],
                    prefix + " reopened sparse_cells");
                check_row_c1_projection(cells[2],
                    prefix + " reopened sparse_cells");
                check_row_a2_projection(cells[3],
                    prefix + " reopened sparse_cells");
                check_row_b2_projection(cells[4],
                    prefix + " reopened sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 3,
                prefix + " reopened row_cells should expose target row records");
            if (row_one.size() == 3) {
                check_row_a1_projection(row_one[0],
                    prefix + " reopened row_cells");
                check_row_b1_projection(row_one[1],
                    prefix + " reopened row_cells");
                check_row_c1_projection(row_one[2],
                    prefix + " reopened row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 2,
                prefix + " reopened row_cells should expose non-target row records");
            if (row_two.size() == 2) {
                check_row_a2_projection(row_two[0],
                    prefix + " reopened row_cells");
                check_row_b2_projection(row_two[1],
                    prefix + " reopened row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " reopened column_cells should expose A1 and A2");
            if (column_one.size() == 2) {
                check_row_a1_projection(column_one[0],
                    prefix + " reopened column_cells");
                check_row_a2_projection(column_one[1],
                    prefix + " reopened column_cells");
            }

            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "row-replacement-unstyled" &&
                    !reopened_a1.has_style(),
                prefix + " reopened output should read replacement A1 without style");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a2.number_value() == 2.0 &&
                    reopened_a2.has_style(),
                prefix + " reopened output should keep non-target A2 styled");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row full replacement save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_row full replacement save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "set_row full replacement should persist target and non-target bounds");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" t="inlineStr"><is><t>row-replacement-unstyled</t></is></c>)",
        "set_row full replacement should persist A1 without a style id");
    check_contains(worksheet_xml, R"(<c r="B1"><v>42</v></c>)",
        "set_row full replacement should persist B1 without a style id");
    check_contains(worksheet_xml, R"(<c r="C1"><f>A1+B1</f></c>)",
        "set_row full replacement should persist C1 without a style id");
    check_contains(worksheet_xml,
        R"(<c r="A2" s=")" + std::to_string(non_default_style.value()) +
            R"("><v>2</v></c>)",
        "set_row full replacement should preserve non-target styled A2");
    check_contains(worksheet_xml, "set-row-full-non-target",
        "set_row full replacement should preserve non-target row cells");
    check_not_contains(worksheet_xml, R"(<c r="A1" s=")",
        "set_row full replacement should not keep the old source style on A1");
    check_not_contains(worksheet_xml, "set-row-full-target-tail",
        "set_row full replacement should omit overwritten target-row tail");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "set_row full replacement should omit overwritten styled A1 number");
    check_reopened_clean_sheet_output(output, "Styled", "set_row full replacement",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_set_row_replacement_output(
                reopened_sheet, "set_row full replacement");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_set_row_replacement_saved_snapshot =
        [&](std::size_t expected_pending_count, std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 5,
                prefix + " should keep saved sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " should expose five saved records");
            if (cells.size() == 5) {
                check_row_a1_projection(cells[0],
                    prefix + " sparse_cells");
                check_row_b1_projection(cells[1],
                    prefix + " sparse_cells");
                check_row_c1_projection(cells[2],
                    prefix + " sparse_cells");
                check_row_a2_projection(cells[3],
                    prefix + " sparse_cells");
                check_row_b2_projection(cells[4],
                    prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                sheet.row_cells(1);
            check(row_one.size() == 3,
                prefix + " should expose target row snapshots");
            if (row_one.size() == 3) {
                check_row_a1_projection(row_one[0],
                    prefix + " row_cells");
                check_row_b1_projection(row_one[1],
                    prefix + " row_cells");
                check_row_c1_projection(row_one[2],
                    prefix + " row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                sheet.row_cells(2);
            check(row_two.size() == 2,
                prefix + " should expose non-target row snapshots");
            if (row_two.size() == 2) {
                check_row_a2_projection(row_two[0],
                    prefix + " row_cells");
                check_row_b2_projection(row_two[1],
                    prefix + " row_cells");
            }

            check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
                prefix + " should keep saved bounds");
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
    check_set_row_replacement_saved_snapshot(
        pending_count_after_save, "set_row full replacement saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_row full replacement no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_row full replacement no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row full replacement no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_row full replacement no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row full replacement no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row full replacement no-op save should keep diagnostics clear");
    check_set_row_replacement_saved_snapshot(
        pending_count_after_save, "set_row full replacement no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_row full replacement no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_row full replacement no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_row full replacement no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row full replacement no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_row full replacement no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_set_row_replacement_output(
                reopened_sheet, "set_row full replacement no-op save");
        });
}

void test_public_worksheet_editor_set_row_accepts_default_style_id_as_unstyled()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-default-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-default-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-default-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-default-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-default-style-post-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("set-row-default-target-tail"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::number(2.0).with_style(non_default_style),
            fastxlsx::CellView::text("set-row-default-non-target"),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_worksheet_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("><v>1</v></c>)",
        "set_row explicit default StyleId source fixture should start with styled A1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_row(1, {
        fastxlsx::CellValue::text("row-default-text").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A1+A2").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
    });

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Text &&
            live_a1.text_value() == "row-default-text" &&
            !live_a1.has_style(),
        "set_row explicit default StyleId should normalize A1 text to unstyled");
    const fastxlsx::CellValue live_b1 = sheet.get_cell("B1");
    check(live_b1.kind() == fastxlsx::CellValueKind::Formula &&
            live_b1.text_value() == "A1+A2" &&
            !live_b1.has_style(),
        "set_row explicit default StyleId should normalize B1 formula to unstyled");
    const fastxlsx::CellValue live_c1 = sheet.get_cell("C1");
    check(live_c1.kind() == fastxlsx::CellValueKind::Blank &&
            !live_c1.has_style(),
        "set_row explicit default StyleId should normalize C1 blank to unstyled");
    const fastxlsx::CellValue live_a2 = sheet.get_cell("A2");
    check(live_a2.kind() == fastxlsx::CellValueKind::Number &&
            live_a2.number_value() == 2.0 &&
            live_a2.has_style() &&
            live_a2.style_id().value() == non_default_style.value(),
        "set_row explicit default StyleId should preserve non-target styled A2");
    check(sheet.cell_count() == 5,
        "set_row explicit default StyleId should keep target and non-target sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "set_row explicit default StyleId should keep target and non-target bounds");
    check(sheet.has_pending_changes(),
        "set_row explicit default StyleId should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 5,
        "set_row explicit default StyleId should expose aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_row explicit default StyleId dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_row explicit default StyleId should keep diagnostics clear");

    const auto check_default_row_a1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "row-default-text" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A1 text");
        };
    const auto check_default_row_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1+A2" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled B1 formula");
        };
    const auto check_default_row_c1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled C1 blank");
        };
    const auto check_default_row_a2 =
        [non_default_style](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 2.0 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                prefix + " should preserve non-target styled A2");
        };
    const auto check_default_row_b2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-row-default-non-target" &&
                    !snapshot.value.has_style(),
                prefix + " should preserve non-target unstyled B2");
        };

    check(sheet.contains_cell("A1") && sheet.contains_cell("B1") &&
            sheet.contains_cell("C1") && sheet.contains_cell("A2") &&
            sheet.contains_cell("B2"),
        "set_row explicit default StyleId should keep represented cells queryable");
    check(!sheet.contains_cell("C2") && !sheet.contains_cell("D4"),
        "set_row explicit default StyleId should keep unrelated missing cells absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_one = sheet.row_cells(1);
    check(live_row_one.size() == 3,
        "set_row explicit default StyleId live row_cells should expose replaced row");
    if (live_row_one.size() == 3) {
        check_default_row_a1(live_row_one[0], "set_row explicit default StyleId live row_cells");
        check_default_row_b1(live_row_one[1], "set_row explicit default StyleId live row_cells");
        check_default_row_c1(live_row_one[2], "set_row explicit default StyleId live row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_two = sheet.row_cells(2);
    check(live_row_two.size() == 2,
        "set_row explicit default StyleId live row_cells should expose non-target row");
    if (live_row_two.size() == 2) {
        check_default_row_a2(live_row_two[0], "set_row explicit default StyleId live row_cells");
        check_default_row_b2(live_row_two[1], "set_row explicit default StyleId live row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_one = sheet.column_cells(1);
    check(live_column_one.size() == 2,
        "set_row explicit default StyleId live column_cells should expose first column");
    if (live_column_one.size() == 2) {
        check_default_row_a1(live_column_one[0], "set_row explicit default StyleId live column_cells");
        check_default_row_a2(live_column_one[1], "set_row explicit default StyleId live column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_two = sheet.column_cells(2);
    check(live_column_two.size() == 2,
        "set_row explicit default StyleId live column_cells should expose second column");
    if (live_column_two.size() == 2) {
        check_default_row_b1(live_column_two[0], "set_row explicit default StyleId live column_cells");
        check_default_row_b2(live_column_two[1], "set_row explicit default StyleId live column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_three = sheet.column_cells(3);
    check(live_column_three.size() == 1,
        "set_row explicit default StyleId live column_cells should expose inserted blank column");
    if (live_column_three.size() == 1) {
        check_default_row_c1(live_column_three[0], "set_row explicit default StyleId live column_cells");
    }
    check(sheet.column_cells(4).empty(),
        "set_row explicit default StyleId live column_cells should keep missing column empty");

    const auto inspect_default_row_output =
        [check_default_row_a1, check_default_row_b1, check_default_row_c1,
            check_default_row_a2, check_default_row_b2, non_default_style](
            fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 5,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                prefix + " reopened output should keep row bounds");
            check(reopened_sheet.contains_cell("A1") &&
                    reopened_sheet.contains_cell("B1") &&
                    reopened_sheet.contains_cell("C1") &&
                    reopened_sheet.contains_cell("A2") &&
                    reopened_sheet.contains_cell("B2"),
                prefix + " reopened output should keep represented cells queryable");
            check(!reopened_sheet.contains_cell("C2") &&
                    !reopened_sheet.contains_cell("D4"),
                prefix + " reopened output should keep unrelated missing cells absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " reopened sparse_cells should expose five records");
            if (cells.size() == 5) {
                check_default_row_a1(cells[0], prefix + " reopened sparse_cells");
                check_default_row_b1(cells[1], prefix + " reopened sparse_cells");
                check_default_row_c1(cells[2], prefix + " reopened sparse_cells");
                check_default_row_a2(cells[3], prefix + " reopened sparse_cells");
                check_default_row_b2(cells[4], prefix + " reopened sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 3,
                prefix + " reopened row_cells should expose replaced row");
            if (row_one.size() == 3) {
                check_default_row_a1(row_one[0], prefix + " reopened row_cells");
                check_default_row_b1(row_one[1], prefix + " reopened row_cells");
                check_default_row_c1(row_one[2], prefix + " reopened row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 2,
                prefix + " reopened row_cells should expose non-target row");
            if (row_two.size() == 2) {
                check_default_row_a2(row_two[0], prefix + " reopened row_cells");
                check_default_row_b2(row_two[1], prefix + " reopened row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " reopened column_cells should expose first column");
            if (column_one.size() == 2) {
                check_default_row_a1(column_one[0], prefix + " reopened column_cells");
                check_default_row_a2(column_one[1], prefix + " reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 2,
                prefix + " reopened column_cells should expose second column");
            if (column_two.size() == 2) {
                check_default_row_b1(column_two[0], prefix + " reopened column_cells");
                check_default_row_b2(column_two[1], prefix + " reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 1,
                prefix + " reopened column_cells should expose inserted blank column");
            if (column_three.size() == 1) {
                check_default_row_c1(column_three[0], prefix + " reopened column_cells");
            }
            check(reopened_sheet.column_cells(4).empty(),
                prefix + " reopened column_cells should keep missing column empty");

            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "row-default-text" &&
                    !reopened_a1.has_style(),
                prefix + " reopened output should read A1 without a style handle");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a2.number_value() == 2.0 &&
                    reopened_a2.has_style() &&
                    reopened_a2.style_id().value() == non_default_style.value(),
                prefix + " reopened output should preserve non-target A2 style");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row explicit default StyleId save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_row explicit default StyleId save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "set_row explicit default StyleId should persist row bounds");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" t="inlineStr"><is><t>row-default-text</t></is></c>)",
        "set_row explicit default StyleId should persist A1 without a style id");
    check_contains(worksheet_xml, R"(<c r="B1"><f>A1+A2</f></c>)",
        "set_row explicit default StyleId should persist B1 without a style id");
    check_contains(worksheet_xml, R"(<c r="C1"/>)",
        "set_row explicit default StyleId should persist C1 without a style id");
    check_contains(worksheet_xml,
        R"(<c r="A2" s=")" + std::to_string(non_default_style.value()) +
            R"("><v>2</v></c>)",
        "set_row explicit default StyleId should preserve non-target styled A2");
    check_not_contains(worksheet_xml, R"(<c r="A1" s=")",
        "set_row explicit default StyleId should not keep the old source style on A1");
    check_not_contains(worksheet_xml, R"(<c r="B1" s=")",
        "set_row explicit default StyleId should not write a default style on B1");
    check_not_contains(worksheet_xml, R"(<c r="C1" s=")",
        "set_row explicit default StyleId should not write a default style on C1");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_row explicit default StyleId should not write default style ids");
    check_not_contains(worksheet_xml, "set-row-default-target-tail",
        "set_row explicit default StyleId should replace prior target-row tail");
    check_reopened_clean_sheet_output(output, "Styled", "set_row explicit default StyleId",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_row_output(
                reopened_sheet, "set_row explicit default StyleId");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_row explicit default StyleId no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_row explicit default StyleId no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row explicit default StyleId no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_row explicit default StyleId no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row explicit default StyleId no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row explicit default StyleId no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_row explicit default StyleId no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_row explicit default StyleId no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_row explicit default StyleId no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row explicit default StyleId no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_row explicit default StyleId no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_row_output(
                reopened_sheet, "set_row explicit default StyleId no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "set_row explicit default StyleId second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_row explicit default StyleId second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row explicit default StyleId second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_row explicit default StyleId second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row explicit default StyleId second no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row explicit default StyleId second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "set_row explicit default StyleId second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "set_row explicit default StyleId second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "set_row explicit default StyleId second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_row explicit default StyleId second no-op save should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_row explicit default StyleId second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row explicit default StyleId second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled",
        "set_row explicit default StyleId second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_row_output(
                reopened_sheet,
                "set_row explicit default StyleId second no-op save");
        });

    sheet.set_row(1, {
        fastxlsx::CellValue::formula("A2+B2").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::text("row-default-post-noop")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::boolean(true).with_style(fastxlsx::StyleId {}),
    });
    const fastxlsx::CellValue post_noop_live_a1 = sheet.get_cell("A1");
    check(post_noop_live_a1.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_live_a1.text_value() == "A2+B2" &&
            !post_noop_live_a1.has_style(),
        "set_row explicit default StyleId post-noop edit should keep A1 formula unstyled");
    const fastxlsx::CellValue post_noop_live_d1 = sheet.get_cell("D1");
    check(post_noop_live_d1.kind() == fastxlsx::CellValueKind::Boolean &&
            post_noop_live_d1.boolean_value() &&
            !post_noop_live_d1.has_style(),
        "set_row explicit default StyleId post-noop edit should keep D1 boolean unstyled");
    const fastxlsx::CellValue post_noop_live_a2 = sheet.get_cell("A2");
    check(post_noop_live_a2.kind() == fastxlsx::CellValueKind::Number &&
            post_noop_live_a2.number_value() == 2.0 &&
            post_noop_live_a2.has_style() &&
            post_noop_live_a2.style_id().value() == non_default_style.value(),
        "set_row explicit default StyleId post-noop edit should preserve non-target styled A2");
    check(sheet.cell_count() == 6,
        "set_row explicit default StyleId post-noop edit should expand sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "set_row explicit default StyleId post-noop edit should expand row bounds");
    check(sheet.has_pending_changes(),
        "set_row explicit default StyleId post-noop edit should dirty the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_row explicit default StyleId post-noop edit should not record a handoff before save");
    check(editor.pending_materialized_cell_count() == 6,
        "set_row explicit default StyleId post-noop edit should expose dirty sparse count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "set_row explicit default StyleId post-noop edit dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_row explicit default StyleId post-noop edit should keep diagnostics clear");

    const auto check_post_noop_row_a1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A2+B2" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A1 formula");
        };
    const auto check_post_noop_row_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "row-default-post-noop" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled B1 text");
        };
    const auto check_post_noop_row_c1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled C1 blank");
        };
    const auto check_post_noop_row_d1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 4 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Boolean &&
                    snapshot.value.boolean_value() &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled D1 boolean");
        };

    const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_row_one =
        sheet.row_cells(1);
    check(post_noop_row_one.size() == 4,
        "set_row explicit default StyleId post-noop edit row_cells should expose replaced row");
    if (post_noop_row_one.size() == 4) {
        check_post_noop_row_a1(post_noop_row_one[0],
            "set_row explicit default StyleId post-noop edit row_cells");
        check_post_noop_row_b1(post_noop_row_one[1],
            "set_row explicit default StyleId post-noop edit row_cells");
        check_post_noop_row_c1(post_noop_row_one[2],
            "set_row explicit default StyleId post-noop edit row_cells");
        check_post_noop_row_d1(post_noop_row_one[3],
            "set_row explicit default StyleId post-noop edit row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_row_two =
        sheet.row_cells(2);
    check(post_noop_row_two.size() == 2,
        "set_row explicit default StyleId post-noop edit row_cells should keep non-target row");
    if (post_noop_row_two.size() == 2) {
        check_default_row_a2(post_noop_row_two[0],
            "set_row explicit default StyleId post-noop edit row_cells");
        check_default_row_b2(post_noop_row_two[1],
            "set_row explicit default StyleId post-noop edit row_cells");
    }

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "set_row explicit default StyleId post-noop save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "set_row explicit default StyleId post-noop save should record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row explicit default StyleId post-noop save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_row explicit default StyleId post-noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row explicit default StyleId post-noop save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row explicit default StyleId post-noop save should keep diagnostics clear");

    const auto post_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row explicit default StyleId post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_row explicit default StyleId post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_row explicit default StyleId post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "set_row explicit default StyleId post-noop save should leave the second no-op output unchanged");
    check(post_noop_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_row explicit default StyleId post-noop save should preserve source styles.xml bytes");

    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "set_row explicit default StyleId post-noop save should expand row bounds");
    check_contains(post_noop_worksheet_xml, R"(<c r="A1"><f>A2+B2</f></c>)",
        "set_row explicit default StyleId post-noop save should persist A1 formula without a style id");
    check_contains(post_noop_worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>row-default-post-noop</t></is></c>)",
        "set_row explicit default StyleId post-noop save should persist B1 without a style id");
    check_contains(post_noop_worksheet_xml, R"(<c r="C1"/>)",
        "set_row explicit default StyleId post-noop save should persist C1 without a style id");
    check_contains(post_noop_worksheet_xml, R"(<c r="D1" t="b"><v>1</v></c>)",
        "set_row explicit default StyleId post-noop save should persist D1 without a style id");
    check_contains(post_noop_worksheet_xml,
        R"(<c r="A2" s=")" + std::to_string(non_default_style.value()) +
            R"("><v>2</v></c>)",
        "set_row explicit default StyleId post-noop save should preserve non-target styled A2");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="A1" s=")",
        "set_row explicit default StyleId post-noop save should not revive the old source style on A1");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="B1" s=")",
        "set_row explicit default StyleId post-noop save should not write a default style on B1");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="C1" s=")",
        "set_row explicit default StyleId post-noop save should not write a default style on C1");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="D1" s=")",
        "set_row explicit default StyleId post-noop save should not write a default style on D1");
    check_not_contains(post_noop_worksheet_xml, R"(s="0")",
        "set_row explicit default StyleId post-noop save should not write default style ids");
    check_not_contains(post_noop_worksheet_xml, "row-default-text",
        "set_row explicit default StyleId post-noop save should replace the earlier A1 text");

    check_reopened_clean_sheet_output(
        post_noop_output, "Styled",
        "set_row explicit default StyleId post-noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "set_row explicit default StyleId post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "set_row explicit default StyleId post-noop reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 4,
                "set_row explicit default StyleId post-noop reopened row_cells should expose row one");
            if (row_one.size() == 4) {
                check_post_noop_row_a1(row_one[0],
                    "set_row explicit default StyleId post-noop reopened row_cells");
                check_post_noop_row_b1(row_one[1],
                    "set_row explicit default StyleId post-noop reopened row_cells");
                check_post_noop_row_c1(row_one[2],
                    "set_row explicit default StyleId post-noop reopened row_cells");
                check_post_noop_row_d1(row_one[3],
                    "set_row explicit default StyleId post-noop reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 2,
                "set_row explicit default StyleId post-noop reopened row_cells should expose row two");
            if (row_two.size() == 2) {
                check_default_row_a2(row_two[0],
                    "set_row explicit default StyleId post-noop reopened row_cells");
                check_default_row_b2(row_two[1],
                    "set_row explicit default StyleId post-noop reopened row_cells");
            }
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_a1.text_value() == "A2+B2" &&
                    !reopened_a1.has_style(),
                "set_row explicit default StyleId post-noop reopened output should read A1 formula without a style handle");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a2.number_value() == 2.0 &&
                    reopened_a2.has_style() &&
                    reopened_a2.style_id().value() == non_default_style.value(),
                "set_row explicit default StyleId post-noop reopened output should preserve non-target A2 style");
        });
}

void test_public_worksheet_editor_set_row_style_rejection_preserves_dirty_session()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-style-rejection-dirty-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-style-rejection-dirty-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-style-rejection-dirty-noop-output.xlsx");
    const std::filesystem::path recovery_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-style-rejection-dirty-recovery-output.xlsx");
    const std::filesystem::path recovery_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-style-rejection-dirty-recovery-noop-output.xlsx");

    fastxlsx::StyleId source_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        source_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(source_style),
            fastxlsx::CellView::text("set-row-dirty-source-b1"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::text("set-row-dirty-source-a2"),
        });
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_row dirty style rejection source fixture should start with styled A1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    const auto check_styled_a1 =
        [source_style](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 1.0 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == source_style.value(),
                prefix + " should expose source-styled A1");
        };
    const auto check_source_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-row-dirty-source-b1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled source B1");
        };
    const auto check_dirty_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-row-dirty-kept" &&
                    !snapshot.value.has_style(),
                prefix + " should expose preserved dirty A2");
        };
    const auto check_dirty_b2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose preserved dirty B2 formula");
        };
    const auto check_recovered_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-row-dirty-recovered" &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered dirty A2");
        };
    const auto check_recovered_b2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered dirty B2 blank");
        };
    const auto check_recovered_c2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Boolean &&
                    snapshot.value.boolean_value() &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered dirty C2 boolean");
        };
    const auto check_dirty_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 4,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 2, 2,
                prefix + " should keep the represented bounds");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2") &&
                    current_sheet.contains_cell("B2"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("C2") &&
                    !current_sheet.contains_cell("A3"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 4,
                prefix + " sparse_cells should expose four represented records");
            if (cells.size() == 4) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_source_b1(cells[1], prefix + " sparse_cells");
                check_dirty_a2(cells[2], prefix + " sparse_cells");
                check_dirty_b2(cells[3], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                current_sheet.row_cells(2);
            check(row_two.size() == 2,
                prefix + " row_cells should expose dirty row two");
            if (row_two.size() == 2) {
                check_dirty_a2(row_two[0], prefix + " row_cells");
                check_dirty_b2(row_two[1], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                current_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " column_cells should expose column one");
            if (column_one.size() == 2) {
                check_styled_a1(column_one[0], prefix + " column_cells");
                check_dirty_a2(column_one[1], prefix + " column_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.0 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve source-styled A1");
            const fastxlsx::CellValue a2 = current_sheet.get_cell("A2");
            check(a2.kind() == fastxlsx::CellValueKind::Text &&
                    a2.text_value() == "set-row-dirty-kept" &&
                    !a2.has_style(),
                prefix + " get_cell should preserve dirty A2 without a style");
            const fastxlsx::CellValue b2 = current_sheet.get_cell("B2");
            check(b2.kind() == fastxlsx::CellValueKind::Formula &&
                    b2.text_value() == "A1" &&
                    !b2.has_style(),
                prefix + " get_cell should preserve dirty B2 without a style");
        };
    const auto check_recovery_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 5,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 2, 3,
                prefix + " should keep the represented bounds");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2") &&
                    current_sheet.contains_cell("B2") &&
                    current_sheet.contains_cell("C2"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("A3") &&
                    !current_sheet.contains_cell("D2"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " sparse_cells should expose five represented records");
            if (cells.size() == 5) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_source_b1(cells[1], prefix + " sparse_cells");
                check_recovered_a2(cells[2], prefix + " sparse_cells");
                check_recovered_b2(cells[3], prefix + " sparse_cells");
                check_recovered_c2(cells[4], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                current_sheet.row_cells(2);
            check(row_two.size() == 3,
                prefix + " row_cells should expose recovered row two");
            if (row_two.size() == 3) {
                check_recovered_a2(row_two[0], prefix + " row_cells");
                check_recovered_b2(row_two[1], prefix + " row_cells");
                check_recovered_c2(row_two[2], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                current_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " column_cells should expose column one");
            if (column_one.size() == 2) {
                check_styled_a1(column_one[0], prefix + " column_cells");
                check_recovered_a2(column_one[1], prefix + " column_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.0 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve source-styled A1");
            const fastxlsx::CellValue a2 = current_sheet.get_cell("A2");
            check(a2.kind() == fastxlsx::CellValueKind::Text &&
                    a2.text_value() == "set-row-dirty-recovered" &&
                    !a2.has_style(),
                prefix + " get_cell should preserve recovered A2 without a style");
            const fastxlsx::CellValue b2 = current_sheet.get_cell("B2");
            check(b2.kind() == fastxlsx::CellValueKind::Blank &&
                    !b2.has_style(),
                prefix + " get_cell should preserve recovered B2 without a style");
            const fastxlsx::CellValue c2 = current_sheet.get_cell("C2");
            check(c2.kind() == fastxlsx::CellValueKind::Boolean &&
                    c2.boolean_value() &&
                    !c2.has_style(),
                prefix + " get_cell should preserve recovered C2 without a style");
        };

    sheet.set_row(2, {
        fastxlsx::CellValue::text("set-row-dirty-kept").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A1").with_style(fastxlsx::StyleId {}),
    });
    check(!editor.last_edit_error().has_value(),
        "set_row dirty style rejection setup should start diagnostic-clean");
    check(sheet.has_pending_changes(),
        "set_row dirty style rejection setup should dirty the materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_row dirty style rejection setup");
    check_dirty_views(sheet, "set_row dirty style rejection setup");

    bool failed = false;
    try {
        sheet.set_row(3, {
            fastxlsx::CellValue::text("set-row-dirty-rejected")
                .with_style(source_style),
        });
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "StyleId",
            "set_row dirty style rejection should expose the unsupported StyleId boundary");
    }
    check(failed,
        "set_row dirty style rejection should reject caller-supplied non-default StyleId values");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_row dirty style rejection should retain the public StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "set_row dirty style rejection should keep the prior dirty materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_row dirty style rejection",
        editor.last_edit_error());
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row dirty style rejection should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_row dirty style rejection live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_save, "set_row dirty style rejection save");
    check(!sheet.has_pending_changes(),
        "set_row dirty style rejection save should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "set_row dirty style rejection save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_row dirty style rejection save should clear dirty materialized diagnostics");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_row dirty style rejection save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row dirty style rejection save should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_row dirty style rejection saved handle");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_row dirty style rejection save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "set-row-dirty-kept",
        "set_row dirty style rejection save should persist prior dirty A2");
    check_contains(worksheet_xml, R"(<c r="B2"><f>A1</f></c>)",
        "set_row dirty style rejection save should persist prior dirty B2 formula");
    check_not_contains(worksheet_xml, "set-row-dirty-rejected",
        "set_row dirty style rejection save should not leak rejected payloads");
    check_contains(worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_row dirty style rejection save should keep source A1 styled");
    check_not_contains(worksheet_xml, R"(<c r="A2" s=")",
        "set_row dirty style rejection save should keep dirty A2 unstyled");
    check_not_contains(worksheet_xml, R"(<c r="B2" s=")",
        "set_row dirty style rejection save should keep dirty B2 unstyled");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_row dirty style rejection save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row dirty style rejection save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        output, "Styled", "set_row dirty style rejection save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "set_row dirty style rejection save");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_row dirty style rejection noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_row dirty style rejection noop save");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_row dirty style rejection noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "set_row dirty style rejection noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_row dirty style rejection noop save should keep dirty diagnostics clear");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_row dirty style rejection noop save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row dirty style rejection noop save should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_row dirty style rejection noop saved handle");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_row dirty style rejection noop output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row dirty style rejection noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_row dirty style rejection noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "set_row dirty style rejection noop save");
        });

    sheet.set_row(2, {
        fastxlsx::CellValue::text("set-row-dirty-recovered")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::boolean(true).with_style(fastxlsx::StyleId {}),
    });
    check(!editor.last_edit_error().has_value(),
        "set_row dirty style rejection recovery should clear the retained StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "set_row dirty style rejection recovery should dirty the materialized sheet again");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "set_row dirty style rejection recovery");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row dirty style rejection recovery should not queue replacement diagnostics");
    check_recovery_views(sheet, "set_row dirty style rejection recovery live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_recovery_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(recovery_output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_save,
        "set_row dirty style rejection recovery save");
    check(!sheet.has_pending_changes(),
        "set_row dirty style rejection recovery save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "set_row dirty style rejection recovery save should record one more materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_row dirty style rejection recovery save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row dirty style rejection recovery save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_row dirty style rejection recovery save should not queue replacement diagnostics");
    check_recovery_views(sheet, "set_row dirty style rejection recovery saved handle");

    const auto recovery_entries = fastxlsx::test::read_zip_entries(recovery_output);
    check(recovery_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_row dirty style rejection recovery save should preserve source styles.xml bytes");
    const std::string recovery_worksheet_xml =
        recovery_entries.at("xl/worksheets/sheet1.xml");
    check_contains(recovery_worksheet_xml, "set-row-dirty-source-b1",
        "set_row dirty style rejection recovery save should preserve source B1");
    check_contains(recovery_worksheet_xml, "set-row-dirty-recovered",
        "set_row dirty style rejection recovery save should persist recovered A2");
    check_contains(recovery_worksheet_xml, R"(<c r="B2"/>)",
        "set_row dirty style rejection recovery save should persist recovered B2 blank");
    check_contains(recovery_worksheet_xml, R"(<c r="C2" t="b"><v>1</v></c>)",
        "set_row dirty style rejection recovery save should persist recovered C2 boolean");
    check_not_contains(recovery_worksheet_xml, "set-row-dirty-kept",
        "set_row dirty style rejection recovery save should replace prior dirty A2");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B2"><f>A1</f></c>)",
        "set_row dirty style rejection recovery save should replace prior dirty B2 formula");
    check_not_contains(recovery_worksheet_xml, "set-row-dirty-source-a2",
        "set_row dirty style rejection recovery save should not revive source A2");
    check_not_contains(recovery_worksheet_xml, "set-row-dirty-rejected",
        "set_row dirty style rejection recovery save should not leak rejected payloads");
    check_contains(recovery_worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_row dirty style rejection recovery save should keep source A1 styled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="A2" s=")",
        "set_row dirty style rejection recovery save should keep recovered A2 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B2" s=")",
        "set_row dirty style rejection recovery save should keep recovered B2 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="C2" s=")",
        "set_row dirty style rejection recovery save should keep recovered C2 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(s="0")",
        "set_row dirty style rejection recovery save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row dirty style rejection recovery save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_output, "Styled", "set_row dirty style rejection recovery save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "set_row dirty style rejection recovery save");
        });

    const std::size_t pending_count_after_recovery_save =
        editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_recovery_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_recovery_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(recovery_noop_output);
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_recovery_noop,
        "set_row dirty style rejection recovery noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_noop,
        "set_row dirty style rejection recovery noop save");
    check(editor.pending_change_count() == pending_count_after_recovery_save,
        "set_row dirty style rejection recovery noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "set_row dirty style rejection recovery noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_row dirty style rejection recovery noop save should keep dirty diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "set_row dirty style rejection recovery noop save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_row dirty style rejection recovery noop save should not queue replacement diagnostics");
    check_recovery_views(
        sheet, "set_row dirty style rejection recovery noop saved handle");
    const auto recovery_noop_entries =
        fastxlsx::test::read_zip_entries(recovery_noop_output);
    check(recovery_noop_entries == recovery_entries,
        "set_row dirty style rejection recovery noop output should match the recovered output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row dirty style rejection recovery noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_noop_output, "Styled",
        "set_row dirty style rejection recovery noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "set_row dirty style rejection recovery noop save");
        });
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_set_row_replaces_sparse_row();
        test_public_worksheet_editor_set_row_replacement_drops_source_styles();
        test_public_worksheet_editor_set_row_accepts_default_style_id_as_unstyled();
        test_public_worksheet_editor_set_row_style_rejection_preserves_dirty_session();
        std::cout << "WorkbookEditor public-state set row tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state set row test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
