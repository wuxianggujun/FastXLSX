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

void test_public_worksheet_editor_append_row_appends_after_sparse_max_row()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-append-row-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-append-row-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-append-row-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-append-row-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.append_row({
        fastxlsx::CellValue::text("appended-a3"),
        fastxlsx::CellValue::number(7.0),
        fastxlsx::CellValue::formula("A3+B1"),
        fastxlsx::CellValue::blank(),
        fastxlsx::CellValue::error("#DIV/0!"),
    });

    check(sheet.cell_count() == 8,
        "append_row should add one sparse record for each appended value");
    check(sheet.get_cell("A3").text_value() == "appended-a3",
        "append_row should write the first value to column A of the next sparse row");
    check(sheet.get_cell("B3").number_value() == 7.0,
        "append_row should write numeric values by input order");
    const fastxlsx::CellValue formula = sheet.get_cell("C3");
    check(formula.kind() == fastxlsx::CellValueKind::Formula
            && formula.text_value() == "A3+B1",
        "append_row should preserve formula text as a formula cell");
    check(sheet.get_cell("D3").kind() == fastxlsx::CellValueKind::Blank,
        "append_row should represent explicit blank values");
    const fastxlsx::CellValue error = sheet.get_cell("E3");
    check(error.kind() == fastxlsx::CellValueKind::Error &&
            error.text_value() == "#DIV/0!",
        "append_row should preserve error payloads as error cells");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(sheet.has_pending_changes(),
        "append_row should dirty the materialized worksheet when values are appended");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "append_row should report the dirty materialized worksheet name");
    check(editor.pending_materialized_cell_count() == 8,
        "append_row should contribute appended sparse records to aggregate diagnostics");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "append_row should contribute appended sparse records to aggregate memory diagnostics");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "append_row dirty summary");
    check(!editor.last_edit_error().has_value(),
        "successful append_row should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row save should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E3"/>)",
        "append_row should extend the dirty worksheet dimension to the appended row");
    check_contains(worksheet_xml,
        R"(<row r="3"><c r="A3" t="inlineStr"><is><t>appended-a3</t></is></c>)",
        "append_row should persist text in the appended sparse row");
    check_contains(worksheet_xml, R"(<c r="B3"><v>7</v></c>)",
        "append_row should persist numeric cells in input order");
    check_contains(worksheet_xml, R"(<c r="C3"><f>A3+B1</f></c>)",
        "append_row should persist formula cells in input order");
    check_contains(worksheet_xml, R"(<c r="D3"/>)",
        "append_row should persist explicit blank cells in input order");
    check_contains(worksheet_xml, R"(<c r="E3" t="e"><v>#DIV/0!</v></c>)",
        "append_row should persist error cells in input order");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "append_row should preserve untouched worksheets");
    const auto inspect_append_row_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "append_row reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 5,
                "append_row reopened output should expose appended row bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a1",
                "append_row reopened output should keep source-backed A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "append_row reopened output should keep source-backed B1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "placeholder-a2",
                "append_row reopened output should keep source-backed A2");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "appended-a3",
                "append_row reopened output should read appended text");
            const fastxlsx::CellValue reopened_b3 = reopened_sheet.get_cell("B3");
            check(reopened_b3.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b3.number_value() == 7.0,
                "append_row reopened output should read appended number");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c3.text_value() == "A3+B1",
                "append_row reopened output should read appended formula");
            const fastxlsx::CellValue reopened_d3 = reopened_sheet.get_cell("D3");
            check(reopened_d3.kind() == fastxlsx::CellValueKind::Blank,
                "append_row reopened output should read appended explicit blank");
            const fastxlsx::CellValue reopened_e3 = reopened_sheet.get_cell("E3");
            check(reopened_e3.kind() == fastxlsx::CellValueKind::Error &&
                    reopened_e3.text_value() == "#DIV/0!",
                "append_row reopened output should read appended error");
        };
    check_reopened_clean_sheet_output(output, "Data", "append_row",
        inspect_append_row_output);
    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_append_row_saved_snapshot =
        [&](std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 8,
                prefix + " should keep the saved sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 8,
                prefix + " should expose all saved sparse records");
            if (cells.size() == 8) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "placeholder-a1",
                    prefix + " should keep source A1 first");
                check(cells[1].reference.row == 1 &&
                        cells[1].reference.column == 2 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[1].value.number_value() == 1.0,
                    prefix + " should keep source B1 second");
                check(cells[2].reference.row == 2 &&
                        cells[2].reference.column == 1 &&
                        cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[2].value.text_value() == "placeholder-a2",
                    prefix + " should keep source A2 third");
                check(cells[3].reference.row == 3 &&
                        cells[3].reference.column == 1 &&
                        cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[3].value.text_value() == "appended-a3",
                    prefix + " should keep appended A3 fourth");
                check(cells[4].reference.row == 3 &&
                        cells[4].reference.column == 2 &&
                        cells[4].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[4].value.number_value() == 7.0,
                    prefix + " should keep appended B3 fifth");
                check(cells[5].reference.row == 3 &&
                        cells[5].reference.column == 3 &&
                        cells[5].value.kind() == fastxlsx::CellValueKind::Formula &&
                        cells[5].value.text_value() == "A3+B1",
                    prefix + " should keep appended C3 formula sixth");
                check(cells[6].reference.row == 3 &&
                        cells[6].reference.column == 4 &&
                        cells[6].value.kind() == fastxlsx::CellValueKind::Blank,
                    prefix + " should keep appended D3 blank seventh");
                check(cells[7].reference.row == 3 &&
                        cells[7].reference.column == 5 &&
                        cells[7].value.kind() == fastxlsx::CellValueKind::Error &&
                        cells[7].value.text_value() == "#DIV/0!",
                    prefix + " should keep appended E3 error last");
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
                prefix + " should expose row-one source cells in order");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                sheet.row_cells(2);
            check(row_two.size() == 1 &&
                    row_two[0].reference.row == 2 &&
                    row_two[0].reference.column == 1 &&
                    row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_two[0].value.text_value() == "placeholder-a2",
                prefix + " should expose row-two source cell");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                sheet.row_cells(3);
            check(row_three.size() == 5 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "appended-a3" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 2 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    row_three[1].value.number_value() == 7.0 &&
                    row_three[2].reference.row == 3 &&
                    row_three[2].reference.column == 3 &&
                    row_three[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    row_three[2].value.text_value() == "A3+B1" &&
                    row_three[3].reference.row == 3 &&
                    row_three[3].reference.column == 4 &&
                    row_three[3].value.kind() == fastxlsx::CellValueKind::Blank &&
                    row_three[4].reference.row == 3 &&
                    row_three[4].reference.column == 5 &&
                    row_three[4].value.kind() == fastxlsx::CellValueKind::Error &&
                    row_three[4].value.text_value() == "#DIV/0!",
                prefix + " should expose appended row in column order");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                sheet.column_cells(1);
            check(column_one.size() == 3 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[0].value.text_value() == "placeholder-a1" &&
                    column_one[1].reference.row == 2 &&
                    column_one[1].reference.column == 1 &&
                    column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[1].value.text_value() == "placeholder-a2" &&
                    column_one[2].reference.row == 3 &&
                    column_one[2].reference.column == 1 &&
                    column_one[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[2].value.text_value() == "appended-a3",
                prefix + " should expose column-one source and appended cells");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                sheet.column_cells(2);
            check(column_two.size() == 2 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_two[0].value.number_value() == 1.0 &&
                    column_two[1].reference.row == 3 &&
                    column_two[1].reference.column == 2 &&
                    column_two[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_two[1].value.number_value() == 7.0,
                prefix + " should expose column-two source and appended numbers");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                sheet.column_cells(3);
            check(column_three.size() == 1 &&
                    column_three[0].reference.row == 3 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    column_three[0].value.text_value() == "A3+B1",
                prefix + " should expose column-three appended formula");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 3 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Blank,
                prefix + " should expose column-four appended blank");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_five =
                sheet.column_cells(5);
            check(column_five.size() == 1 &&
                    column_five[0].reference.row == 3 &&
                    column_five[0].reference.column == 5 &&
                    column_five[0].value.kind() == fastxlsx::CellValueKind::Error &&
                    column_five[0].value.text_value() == "#DIV/0!",
                prefix + " should expose column-five appended error");

            check_cell_range_equals(sheet.used_range(), 1, 1, 3, 5,
                prefix + " should keep saved sparse bounds");
            check(!sheet.try_cell("B2").has_value(),
                prefix + " should not synthesize missing source cells");
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
    check_append_row_saved_snapshot("append_row saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "append_row no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "append_row no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "append_row no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "append_row no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "append_row no-op save should keep diagnostics clear");
    check_append_row_saved_snapshot("append_row no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "append_row no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "append_row no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "append_row no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(noop_output, "Data", "append_row no-op save",
        inspect_append_row_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "append_row second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "append_row second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "append_row second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "append_row second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "append_row second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "append_row second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "append_row second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "append_row second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "append_row second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Data", "append_row second no-op save",
        inspect_append_row_output);
}

void test_public_worksheet_editor_append_row_does_not_inherit_source_styles()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-worksheet-append-row-style-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-append-row-style-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-append-row-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-append-row-style-second-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("append-row-source-tail"),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.append_row({
        fastxlsx::CellValue::text("append-row-unstyled"),
        fastxlsx::CellValue::number(8.0),
        fastxlsx::CellValue::formula("A2+B1"),
        fastxlsx::CellValue::blank(),
    });

    const fastxlsx::CellValue source_a1 = sheet.get_cell("A1");
    check(source_a1.kind() == fastxlsx::CellValueKind::Number &&
            source_a1.number_value() == 1.0 &&
            source_a1.has_style() &&
            source_a1.style_id().value() == non_default_style.value(),
        "append_row should preserve existing source style handles");
    const fastxlsx::CellValue appended_a2 = sheet.get_cell("A2");
    check(appended_a2.kind() == fastxlsx::CellValueKind::Text &&
            appended_a2.text_value() == "append-row-unstyled" &&
            !appended_a2.has_style(),
        "append_row should not inherit styles for appended text cells");
    const fastxlsx::CellValue appended_b2 = sheet.get_cell("B2");
    check(appended_b2.kind() == fastxlsx::CellValueKind::Number &&
            appended_b2.number_value() == 8.0 &&
            !appended_b2.has_style(),
        "append_row should not inherit styles for appended number cells");
    const fastxlsx::CellValue appended_c2 = sheet.get_cell("C2");
    check(appended_c2.kind() == fastxlsx::CellValueKind::Formula &&
            appended_c2.text_value() == "A2+B1" &&
            !appended_c2.has_style(),
        "append_row should not inherit styles for appended formula cells");
    const fastxlsx::CellValue appended_d2 = sheet.get_cell("D2");
    check(appended_d2.kind() == fastxlsx::CellValueKind::Blank &&
            !appended_d2.has_style(),
        "append_row should not inherit styles for appended blank cells");
    check(sheet.cell_count() == 6,
        "styled append_row should keep source and appended sparse records");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "styled append_row should extend bounds to the appended row");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "styled append_row dirty summary");
    check(!editor.last_edit_error().has_value(),
        "successful styled append_row should keep diagnostics clear");

    const auto inspect_styled_append_row_output =
        [non_default_style](fastxlsx::WorksheetEditor& reopened_sheet,
            std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 6,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                prefix + " reopened output should keep appended row bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0 &&
                    reopened_a1.has_style() &&
                    reopened_a1.style_id().value() == non_default_style.value(),
                prefix + " reopened output should preserve source A1 style");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b1.text_value() == "append-row-source-tail" &&
                    !reopened_b1.has_style(),
                prefix + " reopened output should preserve source tail");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "append-row-unstyled" &&
                    !reopened_a2.has_style(),
                prefix + " reopened output should read unstyled appended A2");
            const fastxlsx::CellValue reopened_b2 = reopened_sheet.get_cell("B2");
            check(reopened_b2.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b2.number_value() == 8.0 &&
                    !reopened_b2.has_style(),
                prefix + " reopened output should read unstyled appended B2");
            const fastxlsx::CellValue reopened_c2 = reopened_sheet.get_cell("C2");
            check(reopened_c2.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c2.text_value() == "A2+B1" &&
                    !reopened_c2.has_style(),
                prefix + " reopened output should read unstyled appended C2");
            const fastxlsx::CellValue reopened_d2 = reopened_sheet.get_cell("D2");
            check(reopened_d2.kind() == fastxlsx::CellValueKind::Blank &&
                    !reopened_d2.has_style(),
                prefix + " reopened output should read unstyled appended D2");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled append_row save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "styled append_row save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "styled append_row should persist appended row bounds");
    check_contains(worksheet_xml, R"(<c r="A1" s=")" +
            std::to_string(non_default_style.value()) + R"("><v>1</v></c>)",
        "styled append_row should preserve source styled A1");
    check_contains(worksheet_xml,
        R"(<row r="2"><c r="A2" t="inlineStr"><is><t>append-row-unstyled</t></is></c>)",
        "styled append_row should persist appended A2 without a style id");
    check_contains(worksheet_xml, R"(<c r="B2"><v>8</v></c>)",
        "styled append_row should persist appended B2 without a style id");
    check_contains(worksheet_xml, R"(<c r="C2"><f>A2+B1</f></c>)",
        "styled append_row should persist appended C2 without a style id");
    check_contains(worksheet_xml, R"(<c r="D2"/>)",
        "styled append_row should persist appended D2 without a style id");
    check_not_contains(worksheet_xml, R"(<c r="A2" s=")",
        "styled append_row should not write a style id on appended A2");
    check_not_contains(worksheet_xml, R"(<c r="B2" s=")",
        "styled append_row should not write a style id on appended B2");
    check_not_contains(worksheet_xml, R"(<c r="C2" s=")",
        "styled append_row should not write a style id on appended C2");
    check_not_contains(worksheet_xml, R"(<c r="D2" s=")",
        "styled append_row should not write a style id on appended D2");
    check_reopened_clean_sheet_output(output, "Styled", "styled append_row",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_append_row_output(reopened_sheet, "styled append_row");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    check(!sheet.has_pending_changes(),
        "styled append_row saved handle should be clean after save");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "styled append_row saved handle should clear dirty materialized diagnostics");
    check(editor.pending_change_count() == pending_count_after_save,
        "styled append_row saved handle should keep one materialized handoff");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "styled append_row saved handle should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "styled append_row saved handle should keep diagnostics clear");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "styled append_row no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "styled append_row no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "styled append_row no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "styled append_row no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "styled append_row no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "styled append_row no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "styled append_row no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "styled append_row no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled append_row no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(noop_output, "Styled", "styled append_row no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_append_row_output(
                reopened_sheet, "styled append_row no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "styled append_row second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "styled append_row second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "styled append_row second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "styled append_row second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "styled append_row second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop, "styled append_row second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop, "styled append_row second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "styled append_row second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "styled append_row second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled append_row second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled", "styled append_row second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_styled_append_row_output(
                reopened_sheet, "styled append_row second no-op save");
        });
}

void test_public_worksheet_editor_append_row_accepts_default_style_id_as_unstyled()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-row-default-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-row-default-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-row-default-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-row-default-style-second-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("append-default-source-tail"),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.append_row({
        fastxlsx::CellValue::text("append-default-text").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A2+A1").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
    });

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Number &&
            live_a1.number_value() == 1.0 &&
            live_a1.has_style() &&
            live_a1.style_id().value() == non_default_style.value(),
        "append_row explicit default StyleId should preserve source A1 style");
    const fastxlsx::CellValue live_a2 = sheet.get_cell("A2");
    check(live_a2.kind() == fastxlsx::CellValueKind::Text &&
            live_a2.text_value() == "append-default-text" &&
            !live_a2.has_style(),
        "append_row explicit default StyleId should normalize appended A2 to unstyled");
    const fastxlsx::CellValue live_b2 = sheet.get_cell("B2");
    check(live_b2.kind() == fastxlsx::CellValueKind::Formula &&
            live_b2.text_value() == "A2+A1" &&
            !live_b2.has_style(),
        "append_row explicit default StyleId should normalize appended B2 to unstyled");
    const fastxlsx::CellValue live_c2 = sheet.get_cell("C2");
    check(live_c2.kind() == fastxlsx::CellValueKind::Blank &&
            !live_c2.has_style(),
        "append_row explicit default StyleId should normalize appended C2 blank to unstyled");
    check(sheet.contains_cell("A1") && sheet.contains_cell("B1") &&
            sheet.contains_cell("A2") && sheet.contains_cell("B2") &&
            sheet.contains_cell("C2"),
        "append_row explicit default StyleId should keep source and appended cells queryable");
    check(!sheet.contains_cell("C1") && !sheet.contains_cell("D4"),
        "append_row explicit default StyleId should keep unrelated missing cells absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_one = sheet.row_cells(1);
    check(live_row_one.size() == 2,
        "append_row explicit default StyleId live row_cells should expose source row");
    if (live_row_one.size() == 2) {
        check(live_row_one[0].reference.row == 1 &&
                live_row_one[0].reference.column == 1 &&
                live_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                live_row_one[0].value.number_value() == 1.0 &&
                live_row_one[0].value.has_style() &&
                live_row_one[0].value.style_id().value() == non_default_style.value(),
            "append_row explicit default StyleId live row_cells should preserve source A1 style");
        check(live_row_one[1].reference.row == 1 &&
                live_row_one[1].reference.column == 2 &&
                live_row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                live_row_one[1].value.text_value() == "append-default-source-tail" &&
                !live_row_one[1].value.has_style(),
            "append_row explicit default StyleId live row_cells should preserve source B1");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_two = sheet.row_cells(2);
    check(live_row_two.size() == 3,
        "append_row explicit default StyleId live row_cells should expose appended row");
    if (live_row_two.size() == 3) {
        check(live_row_two[0].reference.row == 2 &&
                live_row_two[0].reference.column == 1 &&
                live_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                live_row_two[0].value.text_value() == "append-default-text" &&
                !live_row_two[0].value.has_style(),
            "append_row explicit default StyleId live row_cells should expose unstyled appended A2");
        check(live_row_two[1].reference.row == 2 &&
                live_row_two[1].reference.column == 2 &&
                live_row_two[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                live_row_two[1].value.text_value() == "A2+A1" &&
                !live_row_two[1].value.has_style(),
            "append_row explicit default StyleId live row_cells should expose unstyled appended B2");
        check(live_row_two[2].reference.row == 2 &&
                live_row_two[2].reference.column == 3 &&
                live_row_two[2].value.kind() == fastxlsx::CellValueKind::Blank &&
                !live_row_two[2].value.has_style(),
            "append_row explicit default StyleId live row_cells should expose unstyled appended C2 blank");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_one = sheet.column_cells(1);
    check(live_column_one.size() == 2,
        "append_row explicit default StyleId live column_cells should expose column A");
    if (live_column_one.size() == 2) {
        check(live_column_one[0].reference.row == 1 &&
                live_column_one[0].reference.column == 1 &&
                live_column_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                live_column_one[0].value.has_style() &&
                live_column_one[0].value.style_id().value() == non_default_style.value(),
            "append_row explicit default StyleId live column_cells should preserve source A1 style");
        check(live_column_one[1].reference.row == 2 &&
                live_column_one[1].reference.column == 1 &&
                live_column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                live_column_one[1].value.text_value() == "append-default-text" &&
                !live_column_one[1].value.has_style(),
            "append_row explicit default StyleId live column_cells should expose unstyled appended A2");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_two = sheet.column_cells(2);
    check(live_column_two.size() == 2,
        "append_row explicit default StyleId live column_cells should expose column B");
    if (live_column_two.size() == 2) {
        check(live_column_two[0].reference.row == 1 &&
                live_column_two[0].reference.column == 2 &&
                live_column_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                live_column_two[0].value.text_value() == "append-default-source-tail" &&
                !live_column_two[0].value.has_style(),
            "append_row explicit default StyleId live column_cells should preserve source B1");
        check(live_column_two[1].reference.row == 2 &&
                live_column_two[1].reference.column == 2 &&
                live_column_two[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                live_column_two[1].value.text_value() == "A2+A1" &&
                !live_column_two[1].value.has_style(),
            "append_row explicit default StyleId live column_cells should expose unstyled appended B2");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_three = sheet.column_cells(3);
    check(live_column_three.size() == 1,
        "append_row explicit default StyleId live column_cells should expose appended blank column");
    if (live_column_three.size() == 1) {
        check(live_column_three[0].reference.row == 2 &&
                live_column_three[0].reference.column == 3 &&
                live_column_three[0].value.kind() == fastxlsx::CellValueKind::Blank &&
                !live_column_three[0].value.has_style(),
            "append_row explicit default StyleId live column_cells should expose unstyled appended C2 blank");
    }
    check(sheet.row_cells(3).empty() && sheet.column_cells(4).empty(),
        "append_row explicit default StyleId live sparse views should keep missing row and column empty");
    check(sheet.cell_count() == 5,
        "append_row explicit default StyleId should keep source plus appended sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "append_row explicit default StyleId should extend bounds to the appended row");
    check(sheet.has_pending_changes(),
        "append_row explicit default StyleId should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 5,
        "append_row explicit default StyleId should expose aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "append_row explicit default StyleId dirty summary");
    check(!editor.last_edit_error().has_value(),
        "append_row explicit default StyleId should keep diagnostics clear");

    const auto inspect_default_append_row_output =
        [non_default_style](
            fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 5,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                prefix + " reopened output should keep appended row bounds");
            check(reopened_sheet.contains_cell("A1") &&
                    reopened_sheet.contains_cell("B1") &&
                    reopened_sheet.contains_cell("A2") &&
                    reopened_sheet.contains_cell("B2") &&
                    reopened_sheet.contains_cell("C2"),
                prefix + " reopened output should keep source and appended cells queryable");
            check(!reopened_sheet.contains_cell("C1") &&
                    !reopened_sheet.contains_cell("D4"),
                prefix + " reopened output should keep unrelated missing cells absent");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0 &&
                    reopened_a1.has_style() &&
                    reopened_a1.style_id().value() == non_default_style.value(),
                prefix + " reopened output should preserve source A1 style");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b1.text_value() == "append-default-source-tail" &&
                    !reopened_b1.has_style(),
                prefix + " reopened output should preserve source B1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "append-default-text" &&
                    !reopened_a2.has_style(),
                prefix + " reopened output should read appended A2 without style");
            const fastxlsx::CellValue reopened_b2 = reopened_sheet.get_cell("B2");
            check(reopened_b2.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_b2.text_value() == "A2+A1" &&
                    !reopened_b2.has_style(),
                prefix + " reopened output should read appended B2 without style");
            const fastxlsx::CellValue reopened_c2 = reopened_sheet.get_cell("C2");
            check(reopened_c2.kind() == fastxlsx::CellValueKind::Blank &&
                    !reopened_c2.has_style(),
                prefix + " reopened output should read appended C2 without style");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " reopened row_cells should expose source row");
            if (row_one.size() == 2) {
                check(row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        row_one[0].value.number_value() == 1.0 &&
                        row_one[0].value.has_style() &&
                        row_one[0].value.style_id().value() == non_default_style.value(),
                    prefix + " reopened row_cells should preserve source A1 style");
                check(row_one[1].reference.row == 1 &&
                        row_one[1].reference.column == 2 &&
                        row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[1].value.text_value() == "append-default-source-tail" &&
                        !row_one[1].value.has_style(),
                    prefix + " reopened row_cells should preserve source B1");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 3,
                prefix + " reopened row_cells should expose appended row");
            if (row_two.size() == 3) {
                check(row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "append-default-text" &&
                        !row_two[0].value.has_style(),
                    prefix + " reopened row_cells should expose appended A2");
                check(row_two[1].reference.row == 2 &&
                        row_two[1].reference.column == 2 &&
                        row_two[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                        row_two[1].value.text_value() == "A2+A1" &&
                        !row_two[1].value.has_style(),
                    prefix + " reopened row_cells should expose appended B2");
                check(row_two[2].reference.row == 2 &&
                        row_two[2].reference.column == 3 &&
                        row_two[2].value.kind() == fastxlsx::CellValueKind::Blank &&
                        !row_two[2].value.has_style(),
                    prefix + " reopened row_cells should expose appended C2 blank");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " reopened column_cells should expose column A");
            if (column_one.size() == 2) {
                check(column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_one[0].value.has_style() &&
                        column_one[0].value.style_id().value() == non_default_style.value(),
                    prefix + " reopened column_cells should preserve source A1 style");
                check(column_one[1].reference.row == 2 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "append-default-text" &&
                        !column_one[1].value.has_style(),
                    prefix + " reopened column_cells should expose appended A2");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 2,
                prefix + " reopened column_cells should expose column B");
            if (column_two.size() == 2) {
                check(column_two[0].reference.row == 1 &&
                        column_two[0].reference.column == 2 &&
                        column_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_two[0].value.text_value() == "append-default-source-tail" &&
                        !column_two[0].value.has_style(),
                    prefix + " reopened column_cells should preserve source B1");
                check(column_two[1].reference.row == 2 &&
                        column_two[1].reference.column == 2 &&
                        column_two[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                        column_two[1].value.text_value() == "A2+A1" &&
                        !column_two[1].value.has_style(),
                    prefix + " reopened column_cells should expose appended B2");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 1,
                prefix + " reopened column_cells should expose appended blank column");
            if (column_three.size() == 1) {
                check(column_three[0].reference.row == 2 &&
                        column_three[0].reference.column == 3 &&
                        column_three[0].value.kind() == fastxlsx::CellValueKind::Blank &&
                        !column_three[0].value.has_style(),
                    prefix + " reopened column_cells should expose appended C2 blank");
            }
            check(reopened_sheet.row_cells(3).empty() &&
                    reopened_sheet.column_cells(4).empty(),
                prefix + " reopened sparse views should keep missing row and column empty");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row explicit default StyleId save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "append_row explicit default StyleId save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "append_row explicit default StyleId should persist appended row bounds");
    check_contains(worksheet_xml, R"(<c r="A1" s=")" +
            std::to_string(non_default_style.value()) + R"("><v>1</v></c>)",
        "append_row explicit default StyleId should preserve source styled A1");
    check_contains(worksheet_xml,
        R"(<row r="2"><c r="A2" t="inlineStr"><is><t>append-default-text</t></is></c>)",
        "append_row explicit default StyleId should persist appended A2 without style");
    check_contains(worksheet_xml, R"(<c r="B2"><f>A2+A1</f></c>)",
        "append_row explicit default StyleId should persist appended B2 without style");
    check_contains(worksheet_xml, R"(<c r="C2"/>)",
        "append_row explicit default StyleId should persist appended C2 without style");
    check_not_contains(worksheet_xml, R"(<c r="A2" s=")",
        "append_row explicit default StyleId should not write a style id on A2");
    check_not_contains(worksheet_xml, R"(<c r="B2" s=")",
        "append_row explicit default StyleId should not write a style id on B2");
    check_not_contains(worksheet_xml, R"(<c r="C2" s=")",
        "append_row explicit default StyleId should not write a style id on C2");
    check_not_contains(worksheet_xml, R"(s="0")",
        "append_row explicit default StyleId should not write default style ids");
    check_reopened_clean_sheet_output(output, "Styled", "append_row explicit default StyleId",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_append_row_output(
                reopened_sheet, "append_row explicit default StyleId");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "append_row explicit default StyleId no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "append_row explicit default StyleId no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "append_row explicit default StyleId no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "append_row explicit default StyleId no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "append_row explicit default StyleId no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "append_row explicit default StyleId no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "append_row explicit default StyleId no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "append_row explicit default StyleId no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "append_row explicit default StyleId no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row explicit default StyleId no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "append_row explicit default StyleId no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_append_row_output(
                reopened_sheet, "append_row explicit default StyleId no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "append_row explicit default StyleId second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "append_row explicit default StyleId second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "append_row explicit default StyleId second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "append_row explicit default StyleId second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "append_row explicit default StyleId second no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "append_row explicit default StyleId second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "append_row explicit default StyleId second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "append_row explicit default StyleId second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "append_row explicit default StyleId second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "append_row explicit default StyleId second no-op save should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "append_row explicit default StyleId second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row explicit default StyleId second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled",
        "append_row explicit default StyleId second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_append_row_output(
                reopened_sheet,
                "append_row explicit default StyleId second no-op save");
        });
}

void test_public_worksheet_editor_append_row_style_rejection_preserves_dirty_session()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-row-style-rejection-dirty-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-row-style-rejection-dirty-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-row-style-rejection-dirty-noop-output.xlsx");
    const std::filesystem::path recovery_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-row-style-rejection-dirty-recovery-output.xlsx");
    const std::filesystem::path recovery_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-row-style-rejection-dirty-recovery-noop-output.xlsx");

    fastxlsx::StyleId source_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        source_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(source_style),
            fastxlsx::CellView::text("append-row-dirty-source-b1"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::text("append-row-dirty-source-a2"),
        });
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "append_row dirty style rejection source fixture should start with styled A1");

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
    const auto check_dirty_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "append-row-dirty-kept" &&
                    !snapshot.value.has_style(),
                prefix + " should expose preserved dirty B1");
        };
    const auto check_source_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "append-row-dirty-source-a2" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled source A2");
        };
    const auto check_recovered_a3 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "append-row-dirty-recovered" &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered appended A3");
        };
    const auto check_recovered_b3 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1+B1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered appended B3 formula");
        };
    const auto check_recovered_c3 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered appended C3 blank");
        };
    const auto check_dirty_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 3,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 2, 2,
                prefix + " should keep the represented bounds");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("A3") &&
                    !current_sheet.contains_cell("B2") &&
                    !current_sheet.contains_cell("C1"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 3,
                prefix + " sparse_cells should expose three represented records");
            if (cells.size() == 3) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_dirty_b1(cells[1], prefix + " sparse_cells");
                check_source_a2(cells[2], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                current_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " row_cells should expose row one");
            if (row_one.size() == 2) {
                check_styled_a1(row_one[0], prefix + " row_cells");
                check_dirty_b1(row_one[1], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                current_sheet.row_cells(2);
            check(row_two.size() == 1,
                prefix + " row_cells should expose row two");
            if (row_two.size() == 1) {
                check_source_a2(row_two[0], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                current_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " column_cells should expose column one");
            if (column_one.size() == 2) {
                check_styled_a1(column_one[0], prefix + " column_cells");
                check_source_a2(column_one[1], prefix + " column_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.0 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve source-styled A1");
            const fastxlsx::CellValue b1 = current_sheet.get_cell("B1");
            check(b1.kind() == fastxlsx::CellValueKind::Text &&
                    b1.text_value() == "append-row-dirty-kept" &&
                    !b1.has_style(),
                prefix + " get_cell should preserve dirty B1 without a style");
            const fastxlsx::CellValue a2 = current_sheet.get_cell("A2");
            check(a2.kind() == fastxlsx::CellValueKind::Text &&
                    a2.text_value() == "append-row-dirty-source-a2" &&
                    !a2.has_style(),
                prefix + " get_cell should preserve source A2");
        };
    const auto check_recovery_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 6,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 3, 3,
                prefix + " should keep the represented bounds");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2") &&
                    current_sheet.contains_cell("A3") &&
                    current_sheet.contains_cell("B3") &&
                    current_sheet.contains_cell("C3"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("B2") &&
                    !current_sheet.contains_cell("D3"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 6,
                prefix + " sparse_cells should expose six represented records");
            if (cells.size() == 6) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_dirty_b1(cells[1], prefix + " sparse_cells");
                check_source_a2(cells[2], prefix + " sparse_cells");
                check_recovered_a3(cells[3], prefix + " sparse_cells");
                check_recovered_b3(cells[4], prefix + " sparse_cells");
                check_recovered_c3(cells[5], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                current_sheet.row_cells(3);
            check(row_three.size() == 3,
                prefix + " row_cells should expose recovered appended row three");
            if (row_three.size() == 3) {
                check_recovered_a3(row_three[0], prefix + " row_cells");
                check_recovered_b3(row_three[1], prefix + " row_cells");
                check_recovered_c3(row_three[2], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                current_sheet.column_cells(2);
            check(column_two.size() == 2,
                prefix + " column_cells should expose column two");
            if (column_two.size() == 2) {
                check_dirty_b1(column_two[0], prefix + " column_cells");
                check_recovered_b3(column_two[1], prefix + " column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                current_sheet.column_cells(3);
            check(column_three.size() == 1,
                prefix + " column_cells should expose recovered C3");
            if (column_three.size() == 1) {
                check_recovered_c3(column_three[0], prefix + " column_cells");
            }
        };

    sheet.set_cell_value("B1",
        fastxlsx::CellValue::text("append-row-dirty-kept")
            .with_style(fastxlsx::StyleId {}));
    check(!editor.last_edit_error().has_value(),
        "append_row dirty style rejection setup should start diagnostic-clean");
    check(sheet.has_pending_changes(),
        "append_row dirty style rejection setup should dirty the materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "append_row dirty style rejection setup");
    check_dirty_views(sheet, "append_row dirty style rejection setup");

    bool failed = false;
    try {
        sheet.append_row({
            fastxlsx::CellValue::text("append-row-dirty-rejected")
                .with_style(source_style),
        });
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "StyleId",
            "append_row dirty style rejection should expose the unsupported StyleId boundary");
    }
    check(failed,
        "append_row dirty style rejection should reject caller-supplied non-default StyleId values");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "append_row dirty style rejection should retain the public StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "append_row dirty style rejection should keep the prior dirty materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "append_row dirty style rejection",
        editor.last_edit_error());
    check_workbook_editor_no_replacement_diagnostics(
        editor, "append_row dirty style rejection should not queue replacement diagnostics");
    check_dirty_views(sheet, "append_row dirty style rejection live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_save, "append_row dirty style rejection save");
    check(!sheet.has_pending_changes(),
        "append_row dirty style rejection save should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "append_row dirty style rejection save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "append_row dirty style rejection save should clear dirty materialized diagnostics");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "append_row dirty style rejection save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "append_row dirty style rejection save should not queue replacement diagnostics");
    check_dirty_views(sheet, "append_row dirty style rejection saved handle");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "append_row dirty style rejection save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "append_row dirty style rejection save should keep dirty bounds");
    check_contains(worksheet_xml, "append-row-dirty-kept",
        "append_row dirty style rejection save should persist prior dirty B1");
    check_not_contains(worksheet_xml, "append-row-dirty-rejected",
        "append_row dirty style rejection save should not leak rejected payloads");
    check_contains(worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "append_row dirty style rejection save should keep source A1 styled");
    check_not_contains(worksheet_xml, R"(<c r="B1" s=")",
        "append_row dirty style rejection save should keep dirty B1 unstyled");
    check_not_contains(worksheet_xml, R"(s="0")",
        "append_row dirty style rejection save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row dirty style rejection save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        output, "Styled", "append_row dirty style rejection save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "append_row dirty style rejection save");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "append_row dirty style rejection noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "append_row dirty style rejection noop save");
    check(editor.pending_change_count() == pending_count_after_save,
        "append_row dirty style rejection noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "append_row dirty style rejection noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "append_row dirty style rejection noop save should keep dirty diagnostics clear");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "append_row dirty style rejection noop save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "append_row dirty style rejection noop save should not queue replacement diagnostics");
    check_dirty_views(sheet, "append_row dirty style rejection noop saved handle");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "append_row dirty style rejection noop output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row dirty style rejection noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "append_row dirty style rejection noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "append_row dirty style rejection noop save");
        });

    sheet.append_row({
        fastxlsx::CellValue::text("append-row-dirty-recovered")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A1+B1").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
    });
    check(!editor.last_edit_error().has_value(),
        "append_row dirty style rejection recovery should clear the retained StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "append_row dirty style rejection recovery should dirty the materialized sheet again");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "append_row dirty style rejection recovery");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "append_row dirty style rejection recovery should not queue replacement diagnostics");
    check_recovery_views(sheet, "append_row dirty style rejection recovery live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_recovery_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(recovery_output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_save,
        "append_row dirty style rejection recovery save");
    check(!sheet.has_pending_changes(),
        "append_row dirty style rejection recovery save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "append_row dirty style rejection recovery save should record one more materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "append_row dirty style rejection recovery save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "append_row dirty style rejection recovery save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "append_row dirty style rejection recovery save should not queue replacement diagnostics");
    check_recovery_views(sheet, "append_row dirty style rejection recovery saved handle");

    const auto recovery_entries = fastxlsx::test::read_zip_entries(recovery_output);
    check(recovery_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "append_row dirty style rejection recovery save should preserve source styles.xml bytes");
    const std::string recovery_worksheet_xml =
        recovery_entries.at("xl/worksheets/sheet1.xml");
    check_contains(recovery_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "append_row dirty style rejection recovery save should extend bounds to C3");
    check_contains(recovery_worksheet_xml, "append-row-dirty-kept",
        "append_row dirty style rejection recovery save should keep prior dirty B1");
    check_contains(recovery_worksheet_xml, "append-row-dirty-source-a2",
        "append_row dirty style rejection recovery save should keep source A2");
    check_contains(recovery_worksheet_xml, "append-row-dirty-recovered",
        "append_row dirty style rejection recovery save should persist recovered A3");
    check_contains(recovery_worksheet_xml, R"(<c r="B3"><f>A1+B1</f></c>)",
        "append_row dirty style rejection recovery save should persist recovered B3 formula");
    check_contains(recovery_worksheet_xml, R"(<c r="C3"/>)",
        "append_row dirty style rejection recovery save should persist recovered C3 blank");
    check_not_contains(recovery_worksheet_xml, "append-row-dirty-source-b1",
        "append_row dirty style rejection recovery save should not revive source B1");
    check_not_contains(recovery_worksheet_xml, "append-row-dirty-rejected",
        "append_row dirty style rejection recovery save should not leak rejected payloads");
    check_contains(recovery_worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "append_row dirty style rejection recovery save should keep source A1 styled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="A3" s=")",
        "append_row dirty style rejection recovery save should keep recovered A3 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B3" s=")",
        "append_row dirty style rejection recovery save should keep recovered B3 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="C3" s=")",
        "append_row dirty style rejection recovery save should keep recovered C3 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(s="0")",
        "append_row dirty style rejection recovery save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row dirty style rejection recovery save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_output, "Styled", "append_row dirty style rejection recovery save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "append_row dirty style rejection recovery save");
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
        "append_row dirty style rejection recovery noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_noop,
        "append_row dirty style rejection recovery noop save");
    check(editor.pending_change_count() == pending_count_after_recovery_save,
        "append_row dirty style rejection recovery noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "append_row dirty style rejection recovery noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "append_row dirty style rejection recovery noop save should keep dirty diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "append_row dirty style rejection recovery noop save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "append_row dirty style rejection recovery noop save should not queue replacement diagnostics");
    check_recovery_views(sheet, "append_row dirty style rejection recovery noop saved handle");
    const auto recovery_noop_entries =
        fastxlsx::test::read_zip_entries(recovery_noop_output);
    check(recovery_noop_entries == recovery_entries,
        "append_row dirty style rejection recovery noop output should match recovered output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_row dirty style rejection recovery noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_noop_output, "Styled",
        "append_row dirty style rejection recovery noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "append_row dirty style rejection recovery noop save");
        });
}

void test_public_worksheet_editor_append_row_noop_and_guardrails()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-append-row-guards-source.xlsx");

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-empty-noop-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-empty-noop-second-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before append_row empty no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before append_row empty no-op");

        sheet.append_row({});
        check(!editor.last_edit_error().has_value(),
            "empty append_row should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty append_row should not dirty a clean materialized worksheet");
        check(!editor.has_pending_changes(),
            "empty append_row should not make the editor dirty");
        check(sheet.cell_count() == 3,
            "empty append_row should not create sparse row metadata");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "empty append_row no-op save should keep the materialized sheet clean");
        check(!editor.has_pending_changes(),
            "empty append_row no-op save should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "empty append_row no-op save should not record a materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "empty append_row no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "empty append_row no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "empty append_row no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_save,
            "empty append_row no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_save,
            "empty append_row no-op save");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "empty append_row no-op save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "empty append_row no-op save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(output, "empty append_row no-op save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "empty append_row second no-op save should keep the materialized sheet clean");
        check(!editor.has_pending_changes(),
            "empty append_row second no-op save should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "empty append_row second no-op save should not record a materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "empty append_row second no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "empty append_row second no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "empty append_row second no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "empty append_row second no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "empty append_row second no-op save");
        check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
            "empty append_row second no-op output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "empty append_row second no-op save should leave the first no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "empty append_row second no-op save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            noop_output, "empty append_row second no-op save");
    }

    {
        const std::filesystem::path style_id_source = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-style-id-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-style-reject-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-style-reject-noop-output.xlsx");
        fastxlsx::StyleId non_default_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_id_source);
            non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
            fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("StyleIds");
            styled_sheet.append_row({
                fastxlsx::CellView::number(1.0).with_style(non_default_style),
            });
            writer.close();
        }

        const auto source_entries = fastxlsx::test::read_zip_entries(source);
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        bool failed = false;
        try {
            sheet.append_row({
                fastxlsx::CellValue::text("append-row-styled-rejected")
                    .with_style(non_default_style),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "StyleId",
                "append_row style rejection should expose the unsupported StyleId boundary");
        }
        check(failed, "append_row should reject caller-supplied non-default StyleId values");
        check(editor.last_edit_error().has_value(),
            "failed append_row style mutation should update last_edit_error");
        if (editor.last_edit_error().has_value()) {
            check_contains(*editor.last_edit_error(), "StyleId",
                "append_row style rejection diagnostic should mention StyleId");
        }
        check_workbook_editor_public_no_pending_state(editor, "append_row style rejection");
        check(!sheet.has_pending_changes(),
            "append_row style rejection should keep the materialized sheet clean");
        check(sheet.cell_count() == 3,
            "append_row style rejection should preserve sparse cell count");
        check(!sheet.try_cell("A3").has_value(),
            "append_row style rejection should not leave rejected appended cells readable");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "append_row style rejection should preserve source A1");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "append_row style rejection should preserve source B1");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "append_row style rejection should preserve source A2");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "append_row style rejection should not queue replacement diagnostics");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(output);
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_save, "append_row style rejection save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_save, "append_row style rejection save");
        check_workbook_editor_public_no_pending_state(
            editor, "append_row style rejection save");
        check(!sheet.has_pending_changes(),
            "append_row style rejection save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "append_row style rejection save should not queue replacement diagnostics");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "append_row style rejection save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row style rejection save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(output, "append_row style rejection save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop, "append_row style rejection noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop, "append_row style rejection noop save");
        check_workbook_editor_public_no_pending_state(
            editor, "append_row style rejection noop save");
        check(!sheet.has_pending_changes(),
            "append_row style rejection noop save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "append_row style rejection noop save should not queue replacement diagnostics");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == source_entries,
            "append_row style rejection noop save should still copy source entries");
        check(noop_entries == output_entries,
            "append_row style rejection noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row style rejection noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            noop_output, "append_row style rejection noop save");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-width-failure-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-width-failure-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        std::vector<fastxlsx::CellValue> too_wide(
            16385, fastxlsx::CellValue::number(1.0));

        bool failed = false;
        try {
            sheet.append_row(too_wide);
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "16384",
                "append_row width failure should expose the Excel column limit");
        }
        check(failed, "append_row should reject more than 16384 values");
        check(!sheet.has_pending_changes(),
            "width failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "width failure should preserve sparse cell count");
        check(!sheet.try_cell("A3").has_value(),
            "width failure should not leak rejected appended cells");
        check(editor.last_edit_error().has_value(),
            "append_row width failure should update last_edit_error");
        check_workbook_editor_public_no_pending_state(
            editor, "append_row width failure");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "append_row width failure should not queue replacement diagnostics");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(output);
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_save, "append_row width failure save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_save, "append_row width failure save");
        check_workbook_editor_public_no_pending_state(
            editor, "append_row width failure save");
        check(!sheet.has_pending_changes(),
            "append_row width failure save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "append_row width failure save should not queue replacement diagnostics");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "append_row width failure save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row width failure save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(output, "append_row width failure save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop, "append_row width failure noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop, "append_row width failure noop save");
        check_workbook_editor_public_no_pending_state(
            editor, "append_row width failure noop save");
        check(!sheet.has_pending_changes(),
            "append_row width failure noop save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "append_row width failure noop save should not queue replacement diagnostics");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == source_entries,
            "append_row width failure noop save should still copy source entries");
        check(noop_entries == output_entries,
            "append_row width failure noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row width failure noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            noop_output, "append_row width failure noop save");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-row-limit-failure-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-row-limit-failure-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        sheet.set_cell("XFD1048576", fastxlsx::CellValue::text("edge-row"));
        const std::size_t dirty_count = sheet.cell_count();

        bool failed = false;
        try {
            sheet.append_row({fastxlsx::CellValue::text("past-edge")});
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "1048576",
                "append_row row-limit failure should expose the Excel row limit");
        }
        check(failed, "append_row should reject appending past the Excel row limit");
        check(sheet.cell_count() == dirty_count,
            "row-limit failure should preserve the previously dirty sparse store");
        check(sheet.get_cell("XFD1048576").text_value() == "edge-row",
            "row-limit failure should preserve the existing edge-row edit");
        check(!sheet.try_cell("A3").has_value(),
            "row-limit failure should not leak rejected appended values");
        check(editor.last_edit_error().has_value(),
            "append_row row-limit failure should update last_edit_error");
        const std::optional<std::string> row_limit_error = editor.last_edit_error();
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "append_row row-limit failure", row_limit_error);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
            workbook_editor_public_catalog_snapshot(editor);
        editor.save_as(output);
        check(editor.last_edit_error() == row_limit_error,
            "append_row row-limit failure save should retain the diagnostic");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_save, "append_row row-limit failure save");
        check(editor.has_pending_changes(),
            "append_row row-limit failure save should retain the saved handoff state");
        check(editor.pending_change_count() == 1,
            "append_row row-limit failure save should retain one saved handoff");
        check(editor.pending_worksheet_edits().empty(),
            "append_row row-limit failure save should not leave dirty summaries");
        check(!sheet.has_pending_changes(),
            "append_row row-limit failure save should flush the edge-row edit");
        check(sheet.cell_count() == dirty_count,
            "append_row row-limit failure save should keep the materialized read model");
        check(editor.pending_materialized_worksheet_names().empty(),
            "append_row row-limit failure save should clear dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "append_row row-limit failure save should clear dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "append_row row-limit failure save should clear dirty materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "append_row row-limit failure save should not queue replacement diagnostics");

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row row-limit failure save should leave the source package unchanged");
        const std::string& worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:XFD1048576"/>)",
            "append_row row-limit failure save should expand the worksheet dimension");
        check_contains(
            worksheet_xml,
            R"(<c r="XFD1048576" t="inlineStr"><is><t>edge-row</t></is></c>)",
            "append_row row-limit failure save should persist the existing edge-row edit");
        check_not_contains(worksheet_xml, "past-edge",
            "append_row row-limit failure save should omit the rejected append payload");

        const auto inspect_row_limit_failure_output =
            [dirty_count](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == dirty_count,
                    "append_row row-limit failure reopen should preserve sparse cell count");
                check_cell_range_equals(
                    reopened_sheet.used_range(), 1, 1, 1048576, 16384,
                    "append_row row-limit failure reopen should preserve max used range");
                check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1",
                    "append_row row-limit failure reopen should preserve source A1");
                check(reopened_sheet.get_cell("B1").number_value() == 1.0,
                    "append_row row-limit failure reopen should preserve source B1");
                check(reopened_sheet.get_cell("A2").text_value() == "placeholder-a2",
                    "append_row row-limit failure reopen should preserve source A2");
                check(reopened_sheet.get_cell("XFD1048576").text_value() == "edge-row",
                    "append_row row-limit failure reopen should preserve the edge-row edit");
                check(!reopened_sheet.try_cell("A3").has_value(),
                    "append_row row-limit failure reopen should not expose rejected values");
            };
        check_reopened_clean_sheet_output(
            output, "Data", "append_row row-limit failure save",
            inspect_row_limit_failure_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop, "append_row row-limit failure noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop, "append_row row-limit failure noop save");
        check(editor.has_pending_changes(),
            "append_row row-limit failure noop save should retain the saved handoff state");
        check(editor.pending_change_count() == 1,
            "append_row row-limit failure noop save should retain one saved handoff");
        check(editor.pending_worksheet_edits().empty(),
            "append_row row-limit failure noop save should not leave dirty summaries");
        check(!sheet.has_pending_changes(),
            "append_row row-limit failure noop save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty(),
            "append_row row-limit failure noop save should keep dirty materialized names clear");
        check(editor.pending_materialized_cell_count() == 0,
            "append_row row-limit failure noop save should keep dirty materialized cell count clear");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "append_row row-limit failure noop save should keep dirty materialized memory clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "append_row row-limit failure noop save should not queue replacement diagnostics");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "append_row row-limit failure noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row row-limit failure noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(
            noop_output, "Data", "append_row row-limit failure noop save",
            inspect_row_limit_failure_output);
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-max-cells-failure-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-max-cells-failure-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool failed = false;
        try {
            sheet.append_row({fastxlsx::CellValue::text("max-cells-rejected")});
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "append_row max_cells failure should expose the guardrail diagnostic");
        }
        check(failed, "append_row should reject new sparse records past max_cells");
        check(editor.last_edit_error().has_value(),
            "append_row max_cells failure should update last_edit_error");
        if (editor.last_edit_error().has_value()) {
            check_contains(*editor.last_edit_error(), "max_cells",
                "append_row max_cells failure diagnostic should mention max_cells");
        }
        check(!sheet.has_pending_changes(),
            "append_row max_cells failure should keep the materialized sheet clean");
        check(sheet.cell_count() == 3,
            "append_row max_cells failure should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "append_row max_cells failure should preserve sparse memory estimate");
        check(!sheet.try_cell("A3").has_value(),
            "append_row max_cells failure should not leave rejected cells readable");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "append_row max_cells failure should preserve source A1");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "append_row max_cells failure should preserve source B1");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "append_row max_cells failure should preserve source A2");
        check_workbook_editor_public_no_pending_state(
            editor, "append_row max_cells failure");
        check(editor.pending_materialized_worksheet_names().empty(),
            "append_row max_cells failure should not expose dirty materialized names");
        check(editor.pending_materialized_cell_count() == 0,
            "append_row max_cells failure should not expose dirty materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "append_row max_cells failure should not expose dirty materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "append_row max_cells failure should not queue replacement diagnostics");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(output);
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_save, "append_row max_cells failure save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_save, "append_row max_cells failure save");
        check_workbook_editor_public_no_pending_state(
            editor, "append_row max_cells failure save");
        check(!sheet.has_pending_changes(),
            "append_row max_cells failure save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty(),
            "append_row max_cells failure save should keep dirty materialized names clear");
        check(editor.pending_materialized_cell_count() == 0,
            "append_row max_cells failure save should keep dirty materialized cell count clear");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "append_row max_cells failure save should keep dirty materialized memory clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "append_row max_cells failure save should not queue replacement diagnostics");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "append_row max_cells failure save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row max_cells failure save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(output, "append_row max_cells failure save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop, "append_row max_cells failure noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop, "append_row max_cells failure noop save");
        check_workbook_editor_public_no_pending_state(
            editor, "append_row max_cells failure noop save");
        check(!sheet.has_pending_changes(),
            "append_row max_cells failure noop save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty(),
            "append_row max_cells failure noop save should keep dirty materialized names clear");
        check(editor.pending_materialized_cell_count() == 0,
            "append_row max_cells failure noop save should keep dirty materialized cell count clear");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "append_row max_cells failure noop save should keep dirty materialized memory clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "append_row max_cells failure noop save should not queue replacement diagnostics");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == source_entries,
            "append_row max_cells failure noop save should still copy source entries");
        check(noop_entries == output_entries,
            "append_row max_cells failure noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row max_cells failure noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            noop_output, "append_row max_cells failure noop save");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-memory-budget-recovery-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-memory-budget-recovery-noop-output.xlsx");
        const std::filesystem::path reacquired_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-memory-budget-recovery-reacquire-noop-output.xlsx");
        const std::filesystem::path second_reacquired_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-memory-budget-recovery-reacquire-second-noop-output.xlsx");
        const std::filesystem::path reacquired_post_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-append-row-memory-budget-recovery-reacquire-post-noop-output.xlsx");
        const std::string rejected_value =
            "append-row-memory-rejected-" + std::string(4096, 'm');
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
        const std::size_t exact_memory_budget = sizing_sheet.estimated_memory_usage();

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.memory_budget_bytes = exact_memory_budget;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();
        check(baseline_memory == exact_memory_budget,
            "append_row memory-budget precondition should load with an exact sparse budget");

        bool failed = false;
        try {
            sheet.append_row({fastxlsx::CellValue::text(rejected_value)});
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
                "append_row memory-budget failure should expose the guardrail diagnostic");
        }
        check(failed, "append_row should enforce memory_budget_bytes on new sparse records");
        check(editor.last_edit_error().has_value(),
            "append_row memory-budget failure should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "append_row memory-budget failure should keep the materialized sheet clean");
        check(!editor.has_pending_changes(),
            "append_row memory-budget failure should keep the editor clean");
        check(sheet.cell_count() == 3,
            "append_row memory-budget failure should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "append_row memory-budget failure should preserve sparse memory estimate");
        check(!sheet.try_cell("A3").has_value(),
            "append_row memory-budget failure should not leave rejected cells readable");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "append_row memory-budget failure should preserve source A1");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "append_row memory-budget failure should preserve source B1");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "append_row memory-budget failure should preserve source A2");
        check_workbook_editor_public_no_pending_state(
            editor, "append_row memory-budget failure");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "append_row memory-budget failure should not queue replacement diagnostics");

        sheet.erase_cell("A2");
        sheet.append_row({fastxlsx::CellValue::text("append-mb-ok")});
        check(!editor.last_edit_error().has_value(),
            "successful append_row memory-budget recovery should clear last_edit_error");
        check(sheet.has_pending_changes(),
            "successful append_row memory-budget recovery should dirty the session");
        check(editor.has_pending_changes(),
            "successful append_row memory-budget recovery should dirty the editor");
        check(sheet.cell_count() == 3,
            "append_row memory-budget recovery should keep the sparse count stable");
        check(sheet.estimated_memory_usage() <= exact_memory_budget,
            "append_row memory-budget recovery should stay within the exact memory budget");
        check(sheet.get_cell("A2").text_value() == "append-mb-ok",
            "append_row memory-budget recovery should write the replacement appended row");
        check(!sheet.try_cell("A3").has_value(),
            "append_row memory-budget recovery should keep the rejected row absent");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row memory-budget recovery save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "append-mb-ok",
            "append_row memory-budget recovery should persist through save_as");
        check_not_contains(worksheet_xml, "append-row-memory-rejected",
            "rejected append_row memory-budget payload should not leak into saved output");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "append_row memory-budget recovery should not resurrect erased source cells");

        const auto inspect_append_row_memory_budget_recovery_output_with_a2 =
            [](fastxlsx::WorksheetEditor& reopened_sheet,
                std::string_view expected_a2,
                std::string_view prefix) {
                const std::string label(prefix);
                check(reopened_sheet.cell_count() == 3,
                    label + " should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                    label + " should keep compact bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "placeholder-a1",
                    label + " should keep source-backed A1");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    label + " should keep source-backed B1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == expected_a2,
                    label + " should read A2");
                check(!reopened_sheet.try_cell("A3").has_value(),
                    label + " should keep rejected A3 absent");
            };
        const auto inspect_append_row_memory_budget_recovery_output =
            [&](fastxlsx::WorksheetEditor& reopened_sheet) {
                inspect_append_row_memory_budget_recovery_output_with_a2(
                    reopened_sheet,
                    "append-mb-ok",
                    "append_row memory-budget recovery reopened output");
            };
        const auto inspect_append_row_memory_budget_post_noop_output =
            [&](fastxlsx::WorksheetEditor& reopened_sheet) {
                inspect_append_row_memory_budget_recovery_output_with_a2(
                    reopened_sheet,
                    "mb-2",
                    "append_row memory-budget recovery post-noop output");
            };
        check_reopened_clean_sheet_output(output, "Data",
            "append_row memory-budget recovery",
            inspect_append_row_memory_budget_recovery_output);
        const auto check_append_row_memory_budget_recovery_saved_snapshot =
            [&](fastxlsx::WorkbookEditor& owner,
                fastxlsx::WorksheetEditor& handle,
                std::size_t expected_pending_count,
                std::string_view prefix) {
                const std::string label(prefix);
                check(handle.cell_count() == 3,
                    label + " should keep the represented sparse count");
                const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                    handle.sparse_cells();
                check(cells.size() == 3,
                    label + " should expose the three represented records");
                if (cells.size() == 3) {
                    check(cells[0].reference.row == 1 &&
                            cells[0].reference.column == 1 &&
                            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[0].value.text_value() == "placeholder-a1",
                        label + " should keep A1 source text first");
                    check(cells[1].reference.row == 1 &&
                            cells[1].reference.column == 2 &&
                            cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                            cells[1].value.number_value() == 1.0,
                        label + " should keep B1 source number second");
                    check(cells[2].reference.row == 2 &&
                            cells[2].reference.column == 1 &&
                            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[2].value.text_value() == "append-mb-ok",
                        label + " should keep recovered A2 text last");
                }
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                    handle.row_cells(1);
                check(row_one.size() == 2 &&
                        row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[0].value.text_value() == "placeholder-a1" &&
                        row_one[1].reference.row == 1 &&
                        row_one[1].reference.column == 2 &&
                        row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        row_one[1].value.number_value() == 1.0,
                    label + " should keep row-one source cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                    handle.row_cells(2);
                check(row_two.size() == 1 &&
                        row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "append-mb-ok",
                    label + " should keep row-two recovery text");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    handle.column_cells(1);
                check(column_one.size() == 2 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "placeholder-a1" &&
                        column_one[1].reference.row == 2 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "append-mb-ok",
                    label + " should keep column-one source and recovery cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                    handle.column_cells(2);
                check(column_two.size() == 1 &&
                        column_two[0].reference.row == 1 &&
                        column_two[0].reference.column == 2 &&
                        column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_two[0].value.number_value() == 1.0,
                    label + " should keep column-two source number");
                check(!handle.try_cell("A3").has_value(),
                    label + " should keep rejected A3 absent");
                check_cell_range_equals(handle.used_range(), 1, 1, 2, 2,
                    label + " should keep compact bounds");
                check(!handle.has_pending_changes(),
                    label + " should keep the handle clean");
                check(owner.pending_change_count() == expected_pending_count,
                    label + " should not add another materialized handoff");
                check(owner.pending_materialized_worksheet_names().empty(),
                    label + " should keep dirty materialized names empty");
                check(owner.pending_materialized_cell_count() == 0,
                    label + " should keep dirty materialized cells empty");
                check(owner.estimated_pending_materialized_memory_usage() == 0,
                    label + " should keep dirty materialized memory empty");
                check(owner.pending_worksheet_edits().empty(),
                    label + " should keep dirty summaries empty");
                check_workbook_editor_no_replacement_diagnostics(
                    owner,
                    label + " should keep replacement diagnostics empty");
                check(!owner.last_edit_error().has_value(),
                    label + " should keep diagnostics clear");
            };

        const std::size_t pending_count_after_save = editor.pending_change_count();
        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "append_row memory-budget recovery noop save should keep the materialized handle clean");
        check(editor.pending_change_count() == pending_count_after_save,
            "append_row memory-budget recovery noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "append_row memory-budget recovery noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "append_row memory-budget recovery noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "append_row memory-budget recovery noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "append_row memory-budget recovery noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "append_row memory-budget recovery noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "append_row memory-budget recovery noop save should keep diagnostics clear");
        check_append_row_memory_budget_recovery_saved_snapshot(
            editor,
            sheet,
            pending_count_after_save,
            "append_row memory-budget recovery saved handle");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "append_row memory-budget recovery noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "append_row memory-budget recovery noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "append_row memory-budget recovery noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row memory-budget recovery noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output, "Data",
            "append_row memory-budget recovery noop save",
            inspect_append_row_memory_budget_recovery_output);

        fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
        fastxlsx::WorksheetEditor reacquired_sheet =
            reacquired_editor.worksheet("Data", options);
        check(!reacquired_editor.last_edit_error().has_value(),
            "append_row memory-budget recovery strict-options reacquire should keep diagnostics clear");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "append_row memory-budget recovery strict-options reacquire");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "append_row memory-budget recovery strict-options reacquire should stay within the original budget");
        inspect_append_row_memory_budget_recovery_output(reacquired_sheet);
        check_append_row_memory_budget_recovery_saved_snapshot(
            reacquired_editor,
            reacquired_sheet,
            0,
            "append_row memory-budget recovery strict-options reacquired handle");

        const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(reacquired_noop_output);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor, reacquired_save_state_before_noop,
            "append_row memory-budget recovery strict-options reacquired noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor, reacquired_catalog_before_noop,
            "append_row memory-budget recovery strict-options reacquired noop save");
        check_append_row_memory_budget_recovery_saved_snapshot(
            reacquired_editor,
            reacquired_sheet,
            0,
            "append_row memory-budget recovery strict-options reacquired noop save");
        const auto reacquired_noop_entries =
            fastxlsx::test::read_zip_entries(reacquired_noop_output);
        check(reacquired_noop_entries == output_entries,
            "append_row memory-budget recovery strict-options reacquired noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "append_row memory-budget recovery strict-options reacquired noop save should leave the saved input unchanged");
        check_reopened_clean_sheet_output(
            reacquired_noop_output, "Data",
            "append_row memory-budget recovery strict-options reacquired noop save",
            inspect_append_row_memory_budget_recovery_output);

        const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(second_reacquired_noop_output);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor, reacquired_save_state_before_second_noop,
            "append_row memory-budget recovery strict-options reacquired second noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor, reacquired_catalog_before_second_noop,
            "append_row memory-budget recovery strict-options reacquired second noop save");
        check_append_row_memory_budget_recovery_saved_snapshot(
            reacquired_editor,
            reacquired_sheet,
            0,
            "append_row memory-budget recovery strict-options reacquired second noop save");
        const auto second_reacquired_noop_entries =
            fastxlsx::test::read_zip_entries(second_reacquired_noop_output);
        check(second_reacquired_noop_entries == reacquired_noop_entries,
            "append_row memory-budget recovery strict-options reacquired second noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "append_row memory-budget recovery strict-options reacquired second noop save should leave the saved input unchanged");
        check_reopened_clean_sheet_output(
            second_reacquired_noop_output, "Data",
            "append_row memory-budget recovery strict-options reacquired second noop save",
            inspect_append_row_memory_budget_recovery_output);

        reacquired_sheet.set_cell_value("A2", fastxlsx::CellValue::text("mb-2"));
        check(!reacquired_editor.last_edit_error().has_value(),
            "append_row memory-budget recovery strict-options reacquired post-noop edit should keep diagnostics clear");
        check(reacquired_sheet.has_pending_changes(),
            "append_row memory-budget recovery strict-options reacquired post-noop edit should dirty the sheet");
        check(reacquired_editor.has_pending_changes(),
            "append_row memory-budget recovery strict-options reacquired post-noop edit should dirty the editor");
        check(reacquired_sheet.cell_count() == 3,
            "append_row memory-budget recovery strict-options reacquired post-noop edit should keep sparse count stable");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "append_row memory-budget recovery strict-options reacquired post-noop edit should stay within the original budget");
        check(reacquired_editor.pending_materialized_cell_count() == 3,
            "append_row memory-budget recovery strict-options reacquired post-noop edit should expose dirty sparse count");
        const fastxlsx::CellValue post_noop_a2 = reacquired_sheet.get_cell("A2");
        check(post_noop_a2.kind() == fastxlsx::CellValueKind::Text &&
                post_noop_a2.text_value() == "mb-2",
            "append_row memory-budget recovery strict-options reacquired post-noop edit should overwrite A2");
        check(!reacquired_sheet.try_cell("A3").has_value(),
            "append_row memory-budget recovery strict-options reacquired post-noop edit should keep rejected A3 absent");
        check_public_state_single_data_dirty_materialized_summary(
            reacquired_editor,
            reacquired_sheet,
            0,
            "append_row memory-budget recovery strict-options reacquired post-noop edit");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "append_row memory-budget recovery strict-options reacquired post-noop edit should not queue replacement diagnostics");

        reacquired_editor.save_as(reacquired_post_noop_output);
        check(!reacquired_sheet.has_pending_changes(),
            "append_row memory-budget recovery strict-options reacquired post-noop save should clean the sheet");
        check(reacquired_editor.pending_change_count() == 1,
            "append_row memory-budget recovery strict-options reacquired post-noop save should keep one handoff");
        check(reacquired_editor.pending_materialized_worksheet_names().empty(),
            "append_row memory-budget recovery strict-options reacquired post-noop save should not expose dirty worksheet names");
        check(reacquired_editor.pending_materialized_cell_count() == 0,
            "append_row memory-budget recovery strict-options reacquired post-noop save should not expose dirty materialized cells");
        check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
            "append_row memory-budget recovery strict-options reacquired post-noop save should not expose dirty materialized memory");
        check(reacquired_editor.pending_worksheet_edits().empty(),
            "append_row memory-budget recovery strict-options reacquired post-noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "append_row memory-budget recovery strict-options reacquired post-noop save should not queue replacement diagnostics");
        check(!reacquired_editor.last_edit_error().has_value(),
            "append_row memory-budget recovery strict-options reacquired post-noop save should keep diagnostics clear");
        const auto reacquired_post_noop_entries =
            fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
        const std::string reacquired_post_noop_xml =
            reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
        check_contains(reacquired_post_noop_xml, "mb-2",
            "append_row memory-budget recovery strict-options reacquired post-noop save should persist the later overwrite");
        check_not_contains(reacquired_post_noop_xml, "append-mb-ok",
            "append_row memory-budget recovery strict-options reacquired post-noop save should replace the earlier A2 text");
        check_not_contains(reacquired_post_noop_xml, "append-row-memory-rejected",
            "append_row memory-budget recovery strict-options reacquired post-noop save should not leak rejected payload");
        check_not_contains(reacquired_post_noop_xml, "placeholder-a2",
            "append_row memory-budget recovery strict-options reacquired post-noop save should keep erased A2 absent");
        check(reacquired_noop_entries == output_entries,
            "append_row memory-budget recovery strict-options reacquired post-noop save should leave first no-op output stable");
        check(second_reacquired_noop_entries == reacquired_noop_entries,
            "append_row memory-budget recovery strict-options reacquired post-noop save should leave second no-op output stable");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "append_row memory-budget recovery strict-options reacquired post-noop save should leave the saved input unchanged");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row memory-budget recovery strict-options reacquired post-noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(
            reacquired_post_noop_output, "Data",
            "append_row memory-budget recovery strict-options reacquired post-noop save",
            inspect_append_row_memory_budget_post_noop_output);
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-append-row-guards-output.xlsx");
        const std::filesystem::path noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-append-row-guards-noop-output.xlsx");
        const std::filesystem::path reacquired_noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-append-row-guards-reacquire-noop-output.xlsx");
        const std::filesystem::path second_reacquired_noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-append-row-guards-reacquire-second-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool failed = false;
        try {
            sheet.append_row({fastxlsx::CellValue::text("max-cells-rejected")});
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "append_row should expose CellStore max_cells guardrail diagnostics");
        }
        check(failed, "append_row should enforce max_cells on new sparse records");
        check(editor.last_edit_error().has_value(),
            "failed append_row max_cells mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "failed append_row max_cells mutation should not dirty the session");
        check(sheet.cell_count() == 3,
            "failed append_row max_cells mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed append_row max_cells mutation should preserve sparse memory estimate");
        check(!sheet.try_cell("A3").has_value(),
            "failed append_row max_cells mutation should not leave rejected cells readable");

        sheet.erase_cell("A2");
        sheet.append_row({fastxlsx::CellValue::text("append-after-erase")});
        check(!editor.last_edit_error().has_value(),
            "successful append_row after budget release should clear last_edit_error");
        check(sheet.get_cell("A2").text_value() == "append-after-erase",
            "append_row should use the current maximum represented row after erase");
        check(sheet.cell_count() == 3,
            "append_row after erase should stay within the exact max_cells budget");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row guardrail recovery save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "append-after-erase",
            "append_row after guardrail recovery should persist through save_as");
        check_not_contains(worksheet_xml, "max-cells-rejected",
            "rejected append_row value should not leak into saved output");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "append_row after erase should not resurrect erased source cells");

        const auto inspect_append_row_guardrail_recovery_output =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    "append_row guardrail recovery reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                    "append_row guardrail recovery reopened output should keep compact bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "placeholder-a1",
                    "append_row guardrail recovery reopened output should keep source-backed A1");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "append_row guardrail recovery reopened output should keep source-backed B1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "append-after-erase",
                    "append_row guardrail recovery reopened output should read appended A2");
                check(!reopened_sheet.try_cell("A3").has_value(),
                    "append_row guardrail recovery reopened output should keep rejected A3 absent");
            };
        check_reopened_clean_sheet_output(output, "Data", "append_row guardrail recovery",
            inspect_append_row_guardrail_recovery_output);
        const auto check_append_row_guardrail_recovery_saved_snapshot =
            [&](fastxlsx::WorkbookEditor& owner,
                fastxlsx::WorksheetEditor& handle,
                std::size_t expected_pending_count,
                std::string_view prefix) {
                const std::string label(prefix);
                check(handle.cell_count() == 3,
                    label + " should keep the represented sparse count");
                const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                    handle.sparse_cells();
                check(cells.size() == 3,
                    label + " should expose the three represented records");
                if (cells.size() == 3) {
                    check(cells[0].reference.row == 1 &&
                            cells[0].reference.column == 1 &&
                            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[0].value.text_value() == "placeholder-a1",
                        label + " should keep A1 source text first");
                    check(cells[1].reference.row == 1 &&
                            cells[1].reference.column == 2 &&
                            cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                            cells[1].value.number_value() == 1.0,
                        label + " should keep B1 source number second");
                    check(cells[2].reference.row == 2 &&
                            cells[2].reference.column == 1 &&
                            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[2].value.text_value() == "append-after-erase",
                        label + " should keep appended A2 recovery text last");
                }
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                    handle.row_cells(1);
                check(row_one.size() == 2 &&
                        row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[0].value.text_value() == "placeholder-a1" &&
                        row_one[1].reference.row == 1 &&
                        row_one[1].reference.column == 2 &&
                        row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        row_one[1].value.number_value() == 1.0,
                    label + " should keep row-one source cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                    handle.row_cells(2);
                check(row_two.size() == 1 &&
                        row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "append-after-erase",
                    label + " should keep row-two recovery text");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    handle.column_cells(1);
                check(column_one.size() == 2 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "placeholder-a1" &&
                        column_one[1].reference.row == 2 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "append-after-erase",
                    label + " should keep column-one source and recovery cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                    handle.column_cells(2);
                check(column_two.size() == 1 &&
                        column_two[0].reference.row == 1 &&
                        column_two[0].reference.column == 2 &&
                        column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_two[0].value.number_value() == 1.0,
                    label + " should keep column-two source number");
                check(!handle.try_cell("A3").has_value(),
                    label + " should keep rejected A3 absent");
                check_cell_range_equals(handle.used_range(), 1, 1, 2, 2,
                    label + " should keep compact bounds");
                check(!handle.has_pending_changes(),
                    label + " should keep the handle clean");
                check(owner.pending_change_count() == expected_pending_count,
                    label + " should not add another materialized handoff");
                check(owner.pending_materialized_worksheet_names().empty(),
                    label + " should keep dirty materialized names empty");
                check(owner.pending_materialized_cell_count() == 0,
                    label + " should keep dirty materialized cells empty");
                check(owner.estimated_pending_materialized_memory_usage() == 0,
                    label + " should keep dirty materialized memory empty");
                check(owner.pending_worksheet_edits().empty(),
                    label + " should keep dirty summaries empty");
                check_workbook_editor_no_replacement_diagnostics(
                    owner,
                    label + " should keep replacement diagnostics empty");
                check(!owner.last_edit_error().has_value(),
                    label + " should keep diagnostics clear");
            };

        const std::size_t pending_count_after_save = editor.pending_change_count();
        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "append_row guardrail recovery noop save should keep the materialized handle clean");
        check(editor.pending_change_count() == pending_count_after_save,
            "append_row guardrail recovery noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "append_row guardrail recovery noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "append_row guardrail recovery noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "append_row guardrail recovery noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "append_row guardrail recovery noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "append_row guardrail recovery noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "append_row guardrail recovery noop save should keep diagnostics clear");
        check_append_row_guardrail_recovery_saved_snapshot(
            editor,
            sheet,
            pending_count_after_save,
            "append_row guardrail recovery saved handle");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "append_row guardrail recovery noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "append_row guardrail recovery noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "append_row guardrail recovery noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "append_row guardrail recovery noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output, "Data",
            "append_row guardrail recovery noop save",
            inspect_append_row_guardrail_recovery_output);

        fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
        fastxlsx::WorksheetEditor reacquired_sheet =
            reacquired_editor.worksheet("Data", options);
        check(!reacquired_editor.last_edit_error().has_value(),
            "append_row guardrail recovery strict-options reacquire should keep diagnostics clear");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "append_row guardrail recovery strict-options reacquire");
        inspect_append_row_guardrail_recovery_output(reacquired_sheet);
        check_append_row_guardrail_recovery_saved_snapshot(
            reacquired_editor,
            reacquired_sheet,
            0,
            "append_row guardrail recovery strict-options reacquired handle");

        const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(reacquired_noop_output);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor, reacquired_save_state_before_noop,
            "append_row guardrail recovery strict-options reacquired noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor, reacquired_catalog_before_noop,
            "append_row guardrail recovery strict-options reacquired noop save");
        check_append_row_guardrail_recovery_saved_snapshot(
            reacquired_editor,
            reacquired_sheet,
            0,
            "append_row guardrail recovery strict-options reacquired noop save");
        const auto reacquired_noop_entries =
            fastxlsx::test::read_zip_entries(reacquired_noop_output);
        check(reacquired_noop_entries == output_entries,
            "append_row guardrail recovery strict-options reacquired noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "append_row guardrail recovery strict-options reacquired noop save should leave the saved input unchanged");
        check_reopened_clean_sheet_output(
            reacquired_noop_output, "Data",
            "append_row guardrail recovery strict-options reacquired noop save",
            inspect_append_row_guardrail_recovery_output);

        const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(second_reacquired_noop_output);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor, reacquired_save_state_before_second_noop,
            "append_row guardrail recovery strict-options reacquired second noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor, reacquired_catalog_before_second_noop,
            "append_row guardrail recovery strict-options reacquired second noop save");
        check_append_row_guardrail_recovery_saved_snapshot(
            reacquired_editor,
            reacquired_sheet,
            0,
            "append_row guardrail recovery strict-options reacquired second noop save");
        const auto second_reacquired_noop_entries =
            fastxlsx::test::read_zip_entries(second_reacquired_noop_output);
        check(second_reacquired_noop_entries == reacquired_noop_entries,
            "append_row guardrail recovery strict-options reacquired second noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "append_row guardrail recovery strict-options reacquired second noop save should leave the saved input unchanged");
        check_reopened_clean_sheet_output(
            second_reacquired_noop_output, "Data",
            "append_row guardrail recovery strict-options reacquired second noop save",
            inspect_append_row_guardrail_recovery_output);
    }
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_append_row_appends_after_sparse_max_row();
        test_public_worksheet_editor_append_row_does_not_inherit_source_styles();
        test_public_worksheet_editor_append_row_accepts_default_style_id_as_unstyled();
        test_public_worksheet_editor_append_row_style_rejection_preserves_dirty_session();
        test_public_worksheet_editor_append_row_noop_and_guardrails();
        std::cout << "WorkbookEditor public-state append row tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state append row test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
