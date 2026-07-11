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

void test_public_worksheet_editor_set_column_replaces_sparse_column()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-column-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-column-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-column-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-column-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_column(1, {
        fastxlsx::CellValue::text("set-column-a1"),
        fastxlsx::CellValue::number(44.0),
        fastxlsx::CellValue::formula("A3+B1"),
        fastxlsx::CellValue::blank(),
        fastxlsx::CellValue::error("#VALUE!"),
    });

    check(sheet.cell_count() == 6,
        "set_column should replace the target sparse column without clearing other columns");
    check(sheet.get_cell("A1").text_value() == "set-column-a1",
        "set_column should write the first value to row one of the target column");
    check(sheet.get_cell("A2").number_value() == 44.0,
        "set_column should write numeric values by input order");
    const fastxlsx::CellValue formula = sheet.get_cell("A3");
    check(formula.kind() == fastxlsx::CellValueKind::Formula
            && formula.text_value() == "A3+B1",
        "set_column should preserve formula text as a formula cell");
    check(sheet.get_cell("A4").kind() == fastxlsx::CellValueKind::Blank,
        "set_column should represent explicit blank values");
    const fastxlsx::CellValue error = sheet.get_cell("A5");
    check(error.kind() == fastxlsx::CellValueKind::Error &&
            error.text_value() == "#VALUE!",
        "set_column should preserve error values as opaque error cells");
    check(sheet.get_cell("B1").number_value() == 1.0,
        "set_column should preserve represented cells outside the target column");
    check(!sheet.try_cell("B2").has_value(),
        "set_column should not synthesize cells outside the target column");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(sheet.has_pending_changes(),
        "set_column should dirty the materialized worksheet when values are replaced");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "set_column should report the dirty materialized worksheet name");
    check(editor.pending_materialized_cell_count() == 6,
        "set_column should contribute the replaced sparse records to aggregate diagnostics");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "set_column should contribute the replaced sparse records to aggregate memory diagnostics");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "set_column dirty summary");
    check(!editor.last_edit_error().has_value(),
        "successful set_column should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column save should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B5"/>)",
        "set_column should refresh the dirty worksheet dimension");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" t="inlineStr"><is><t>set-column-a1</t></is></c>)",
        "set_column should persist target-column text in row order");
    check_contains(worksheet_xml, R"(<c r="A2"><v>44</v></c>)",
        "set_column should persist target-column numeric cells");
    check_contains(worksheet_xml, R"(<c r="A3"><f>A3+B1</f></c>)",
        "set_column should persist target-column formula cells");
    check_contains(worksheet_xml, R"(<c r="A4"/>)",
        "set_column should persist explicit blank cells");
    check_contains(worksheet_xml, R"(<c r="A5" t="e"><v>#VALUE!</v></c>)",
        "set_column should persist target-column error cells");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "set_column should keep non-target column source cells");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "set_column should omit the old target-column row-one text");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "set_column should omit the old target-column row-two text");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "set_column should preserve untouched worksheets");
    const auto inspect_set_column_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "set_column reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 2,
                "set_column reopened output should keep replaced column bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "set-column-a1",
                "set_column reopened output should read target-column text");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a2.number_value() == 44.0,
                "set_column reopened output should read target-column number");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_a3.text_value() == "A3+B1",
                "set_column reopened output should read target-column formula");
            const fastxlsx::CellValue reopened_a4 = reopened_sheet.get_cell("A4");
            check(reopened_a4.kind() == fastxlsx::CellValueKind::Blank,
                "set_column reopened output should read target-column explicit blank");
            const fastxlsx::CellValue reopened_a5 = reopened_sheet.get_cell("A5");
            check(reopened_a5.kind() == fastxlsx::CellValueKind::Error &&
                    reopened_a5.text_value() == "#VALUE!",
                "set_column reopened output should read target-column error");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "set_column reopened output should keep non-target columns");
            check(!reopened_sheet.try_cell("B2").has_value(),
                "set_column reopened output should not synthesize non-target column cells");
        };
    check_reopened_clean_sheet_output(output, "Data", "set_column",
        inspect_set_column_output);
    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_set_column_saved_snapshot =
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
                        cells[0].value.text_value() == "set-column-a1",
                    prefix + " should keep A1 text first");
                check(cells[1].reference.row == 1 &&
                        cells[1].reference.column == 2 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[1].value.number_value() == 1.0,
                    prefix + " should keep non-target B1 second");
                check(cells[2].reference.row == 2 &&
                        cells[2].reference.column == 1 &&
                        cells[2].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[2].value.number_value() == 44.0,
                    prefix + " should keep A2 number third");
                check(cells[3].reference.row == 3 &&
                        cells[3].reference.column == 1 &&
                        cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                        cells[3].value.text_value() == "A3+B1",
                    prefix + " should keep A3 formula fourth");
                check(cells[4].reference.row == 4 &&
                        cells[4].reference.column == 1 &&
                        cells[4].value.kind() == fastxlsx::CellValueKind::Blank,
                    prefix + " should keep A4 blank fifth");
                check(cells[5].reference.row == 5 &&
                        cells[5].reference.column == 1 &&
                        cells[5].value.kind() == fastxlsx::CellValueKind::Error &&
                        cells[5].value.text_value() == "#VALUE!",
                    prefix + " should keep A5 error last");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                sheet.row_cells(1);
            check(row_one.size() == 2 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "set-column-a1" &&
                    row_one[1].reference.row == 1 &&
                    row_one[1].reference.column == 2 &&
                    row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    row_one[1].value.number_value() == 1.0,
                prefix + " should expose row-one target and non-target cells");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                sheet.row_cells(2);
            check(row_two.size() == 1 &&
                    row_two[0].reference.row == 2 &&
                    row_two[0].reference.column == 1 &&
                    row_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    row_two[0].value.number_value() == 44.0,
                prefix + " should expose row-two target number");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                sheet.row_cells(3);
            check(row_three.size() == 1 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    row_three[0].value.text_value() == "A3+B1",
                prefix + " should expose row-three target formula");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_four =
                sheet.row_cells(4);
            check(row_four.size() == 1 &&
                    row_four[0].reference.row == 4 &&
                    row_four[0].reference.column == 1 &&
                    row_four[0].value.kind() == fastxlsx::CellValueKind::Blank,
                prefix + " should expose row-four target blank");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_five =
                sheet.row_cells(5);
            check(row_five.size() == 1 &&
                    row_five[0].reference.row == 5 &&
                    row_five[0].reference.column == 1 &&
                    row_five[0].value.kind() == fastxlsx::CellValueKind::Error &&
                    row_five[0].value.text_value() == "#VALUE!",
                prefix + " should expose row-five target error");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                sheet.column_cells(1);
            check(column_one.size() == 5 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[0].value.text_value() == "set-column-a1" &&
                    column_one[1].reference.row == 2 &&
                    column_one[1].reference.column == 1 &&
                    column_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_one[1].value.number_value() == 44.0 &&
                    column_one[2].reference.row == 3 &&
                    column_one[2].reference.column == 1 &&
                    column_one[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    column_one[2].value.text_value() == "A3+B1" &&
                    column_one[3].reference.row == 4 &&
                    column_one[3].reference.column == 1 &&
                    column_one[3].value.kind() == fastxlsx::CellValueKind::Blank &&
                    column_one[4].reference.row == 5 &&
                    column_one[4].reference.column == 1 &&
                    column_one[4].value.kind() == fastxlsx::CellValueKind::Error &&
                    column_one[4].value.text_value() == "#VALUE!",
                prefix + " should expose the replaced column in row order");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                sheet.column_cells(2);
            check(column_two.size() == 1 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_two[0].value.number_value() == 1.0,
                prefix + " should expose column-two non-target number");

            check_cell_range_equals(sheet.used_range(), 1, 1, 5, 2,
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
    check_set_column_saved_snapshot("set_column saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_column no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "set_column no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column no-op save should keep diagnostics clear");
    check_set_column_saved_snapshot("set_column no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "set_column no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "set_column no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_column no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(noop_output, "Data", "set_column no-op save",
        inspect_set_column_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "set_column second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "set_column second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "set_column second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "set_column second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "set_column second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_column second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Data", "set_column second no-op save",
        inspect_set_column_output);
}

void test_public_worksheet_editor_set_column_replacement_drops_source_styles()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-full-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-full-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-full-style-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::number(10.0).with_style(non_default_style),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::number(2.0).with_style(non_default_style),
            fastxlsx::CellView::text("set-column-full-non-target"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::text("set-column-full-target-tail"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::number(4.0).with_style(non_default_style),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_column(1, {
        fastxlsx::CellValue::text("column-replacement-unstyled"),
        fastxlsx::CellValue::number(43.0),
        fastxlsx::CellValue::formula("A1+B1"),
    });

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Text &&
            live_a1.text_value() == "column-replacement-unstyled" &&
            !live_a1.has_style(),
        "set_column full replacement should drop overwritten source style ids");
    const fastxlsx::CellValue live_a2 = sheet.get_cell("A2");
    check(live_a2.kind() == fastxlsx::CellValueKind::Number &&
            live_a2.number_value() == 43.0 &&
            !live_a2.has_style(),
        "set_column full replacement should write new values without style ids");
    const fastxlsx::CellValue live_a3 = sheet.get_cell("A3");
    check(live_a3.kind() == fastxlsx::CellValueKind::Formula &&
            live_a3.text_value() == "A1+B1" &&
            !live_a3.has_style(),
        "set_column full replacement should insert formulas without style ids");
    const fastxlsx::CellValue live_b1 = sheet.get_cell("B1");
    check(live_b1.kind() == fastxlsx::CellValueKind::Number &&
            live_b1.number_value() == 10.0 &&
            live_b1.has_style() &&
            live_b1.style_id().value() == non_default_style.value(),
        "set_column full replacement should preserve non-target source style ids");
    check(!sheet.try_cell("A4").has_value(),
        "set_column full replacement should remove old target-column tail cells");
    check(sheet.cell_count() == 5,
        "set_column full replacement should replace only the target-column sparse records");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 2,
        "set_column full replacement should keep target and non-target bounds");
    check(sheet.has_pending_changes(),
        "set_column full replacement should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 5,
        "set_column full replacement should expose aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_column full replacement dirty summary");
    check(!editor.last_edit_error().has_value(),
        "successful set_column full replacement should keep diagnostics clear");

    const auto check_column_a1_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "column-replacement-unstyled" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement A1");
        };
    const auto check_column_b1_projection =
        [non_default_style](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 10.0 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                prefix + " should preserve non-target styled B1");
        };
    const auto check_column_a2_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 43.0 &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement A2");
        };
    const auto check_column_b2_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-column-full-non-target" &&
                    !snapshot.value.has_style(),
                prefix + " should preserve non-target unstyled B2");
        };
    const auto check_column_a3_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1+B1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement A3");
        };

    const auto inspect_set_column_replacement_output =
        [check_column_a1_projection, check_column_b1_projection,
            check_column_a2_projection, check_column_b2_projection,
            check_column_a3_projection](
            fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 5,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                prefix + " reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " reopened sparse_cells should expose five records");
            if (cells.size() == 5) {
                check_column_a1_projection(cells[0],
                    prefix + " reopened sparse_cells");
                check_column_b1_projection(cells[1],
                    prefix + " reopened sparse_cells");
                check_column_a2_projection(cells[2],
                    prefix + " reopened sparse_cells");
                check_column_b2_projection(cells[3],
                    prefix + " reopened sparse_cells");
                check_column_a3_projection(cells[4],
                    prefix + " reopened sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " reopened row_cells should expose row-one records");
            if (row_one.size() == 2) {
                check_column_a1_projection(row_one[0],
                    prefix + " reopened row_cells");
                check_column_b1_projection(row_one[1],
                    prefix + " reopened row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 2,
                prefix + " reopened row_cells should expose row-two records");
            if (row_two.size() == 2) {
                check_column_a2_projection(row_two[0],
                    prefix + " reopened row_cells");
                check_column_b2_projection(row_two[1],
                    prefix + " reopened row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 3,
                prefix + " reopened column_cells should expose replacement column");
            if (column_one.size() == 3) {
                check_column_a1_projection(column_one[0],
                    prefix + " reopened column_cells");
                check_column_a2_projection(column_one[1],
                    prefix + " reopened column_cells");
                check_column_a3_projection(column_one[2],
                    prefix + " reopened column_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 2,
                prefix + " reopened column_cells should expose non-target column");
            if (column_two.size() == 2) {
                check_column_b1_projection(column_two[0],
                    prefix + " reopened column_cells");
                check_column_b2_projection(column_two[1],
                    prefix + " reopened column_cells");
            }

            check(!reopened_sheet.try_cell("A4").has_value(),
                prefix + " reopened output should keep old target tail absent");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column full replacement save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_column full replacement save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "set_column full replacement should persist target and non-target bounds");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" t="inlineStr"><is><t>column-replacement-unstyled</t></is></c>)",
        "set_column full replacement should persist A1 without a style id");
    check_contains(worksheet_xml, R"(<c r="B1" s=")" +
            std::to_string(non_default_style.value()) + R"("><v>10</v></c>)",
        "set_column full replacement should preserve non-target styled B1");
    check_contains(worksheet_xml, R"(<c r="A2"><v>43</v></c>)",
        "set_column full replacement should persist A2 without a style id");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>set-column-full-non-target</t></is></c>)",
        "set_column full replacement should preserve non-target B2");
    check_contains(worksheet_xml, R"(<c r="A3"><f>A1+B1</f></c>)",
        "set_column full replacement should persist A3 without a style id");
    check_not_contains(worksheet_xml, R"(<c r="A1" s=")",
        "set_column full replacement should not keep the old source style on A1");
    check_not_contains(worksheet_xml, R"(<c r="A2" s=")",
        "set_column full replacement should not keep the old source style on A2");
    check_not_contains(worksheet_xml, "set-column-full-target-tail",
        "set_column full replacement should omit overwritten target-column tail");
    check_not_contains(worksheet_xml, R"(r="A4")",
        "set_column full replacement should omit old target-column tail rows");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "set_column full replacement should omit overwritten styled A1 number");
    check_not_contains(worksheet_xml, R"(<v>4</v>)",
        "set_column full replacement should omit overwritten styled A4 number");
    check_reopened_clean_sheet_output(output, "Styled", "set_column full replacement",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_set_column_replacement_output(
                reopened_sheet, "set_column full replacement");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_set_column_replacement_saved_snapshot =
        [&](std::size_t expected_pending_count, std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 5,
                prefix + " should keep saved sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " should expose five saved records");
            if (cells.size() == 5) {
                check_column_a1_projection(cells[0],
                    prefix + " sparse_cells");
                check_column_b1_projection(cells[1],
                    prefix + " sparse_cells");
                check_column_a2_projection(cells[2],
                    prefix + " sparse_cells");
                check_column_b2_projection(cells[3],
                    prefix + " sparse_cells");
                check_column_a3_projection(cells[4],
                    prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                sheet.column_cells(1);
            check(column_one.size() == 3,
                prefix + " should expose replacement column snapshots");
            if (column_one.size() == 3) {
                check_column_a1_projection(column_one[0],
                    prefix + " column_cells");
                check_column_a2_projection(column_one[1],
                    prefix + " column_cells");
                check_column_a3_projection(column_one[2],
                    prefix + " column_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                sheet.column_cells(2);
            check(column_two.size() == 2,
                prefix + " should expose non-target column snapshots");
            if (column_two.size() == 2) {
                check_column_b1_projection(column_two[0],
                    prefix + " column_cells");
                check_column_b2_projection(column_two[1],
                    prefix + " column_cells");
            }

            check(!sheet.try_cell("A4").has_value(),
                prefix + " should keep old target tail absent");
            check_cell_range_equals(sheet.used_range(), 1, 1, 3, 2,
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
    check_set_column_replacement_saved_snapshot(
        pending_count_after_save, "set_column full replacement saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_column full replacement no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_column full replacement no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column full replacement no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_column full replacement no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column full replacement no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column full replacement no-op save should keep diagnostics clear");
    check_set_column_replacement_saved_snapshot(
        pending_count_after_save, "set_column full replacement no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_column full replacement no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_column full replacement no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_column full replacement no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column full replacement no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_column full replacement no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_set_column_replacement_output(
                reopened_sheet, "set_column full replacement no-op save");
        });
}

void test_public_worksheet_editor_set_column_accepts_default_style_id_as_unstyled()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-default-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-default-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-default-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-default-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-default-style-post-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::number(10.0).with_style(non_default_style),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::number(2.0).with_style(non_default_style),
            fastxlsx::CellView::text("set-column-default-non-target"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::text("set-column-default-target-tail"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::number(4.0).with_style(non_default_style),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_worksheet_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("><v>1</v></c>)",
        "set_column explicit default StyleId source fixture should start with styled A1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_column(1, {
        fastxlsx::CellValue::text("column-default-text").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A1+B1").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
    });

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Text &&
            live_a1.text_value() == "column-default-text" &&
            !live_a1.has_style(),
        "set_column explicit default StyleId should normalize A1 text to unstyled");
    const fastxlsx::CellValue live_a2 = sheet.get_cell("A2");
    check(live_a2.kind() == fastxlsx::CellValueKind::Formula &&
            live_a2.text_value() == "A1+B1" &&
            !live_a2.has_style(),
        "set_column explicit default StyleId should normalize A2 formula to unstyled");
    const fastxlsx::CellValue live_a3 = sheet.get_cell("A3");
    check(live_a3.kind() == fastxlsx::CellValueKind::Blank &&
            !live_a3.has_style(),
        "set_column explicit default StyleId should normalize A3 blank to unstyled");
    const fastxlsx::CellValue live_b1 = sheet.get_cell("B1");
    check(live_b1.kind() == fastxlsx::CellValueKind::Number &&
            live_b1.number_value() == 10.0 &&
            live_b1.has_style() &&
            live_b1.style_id().value() == non_default_style.value(),
        "set_column explicit default StyleId should preserve non-target styled B1");
    check(!sheet.try_cell("A4").has_value(),
        "set_column explicit default StyleId should remove old target-column tail cells");
    check(sheet.cell_count() == 5,
        "set_column explicit default StyleId should keep target and non-target sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 2,
        "set_column explicit default StyleId should keep target and non-target bounds");
    check(sheet.has_pending_changes(),
        "set_column explicit default StyleId should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 5,
        "set_column explicit default StyleId should expose aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_column explicit default StyleId dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_column explicit default StyleId should keep diagnostics clear");

    const auto check_default_column_a1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "column-default-text" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A1 text");
        };
    const auto check_default_column_b1 =
        [non_default_style](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 10.0 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                prefix + " should preserve non-target styled B1");
        };
    const auto check_default_column_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1+B1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A2 formula");
        };
    const auto check_default_column_b2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-column-default-non-target" &&
                    !snapshot.value.has_style(),
                prefix + " should preserve non-target unstyled B2");
        };
    const auto check_default_column_a3 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A3 blank");
        };

    check(sheet.contains_cell("A1") && sheet.contains_cell("A2") &&
            sheet.contains_cell("A3") && sheet.contains_cell("B1") &&
            sheet.contains_cell("B2"),
        "set_column explicit default StyleId should keep represented cells queryable");
    check(!sheet.contains_cell("A4") && !sheet.contains_cell("D4"),
        "set_column explicit default StyleId should keep unrelated missing cells absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_one = sheet.column_cells(1);
    check(live_column_one.size() == 3,
        "set_column explicit default StyleId live column_cells should expose replaced column");
    if (live_column_one.size() == 3) {
        check_default_column_a1(live_column_one[0],
            "set_column explicit default StyleId live column_cells");
        check_default_column_a2(live_column_one[1],
            "set_column explicit default StyleId live column_cells");
        check_default_column_a3(live_column_one[2],
            "set_column explicit default StyleId live column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_two = sheet.column_cells(2);
    check(live_column_two.size() == 2,
        "set_column explicit default StyleId live column_cells should expose non-target column");
    if (live_column_two.size() == 2) {
        check_default_column_b1(live_column_two[0],
            "set_column explicit default StyleId live column_cells");
        check_default_column_b2(live_column_two[1],
            "set_column explicit default StyleId live column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_one = sheet.row_cells(1);
    check(live_row_one.size() == 2,
        "set_column explicit default StyleId live row_cells should expose first row");
    if (live_row_one.size() == 2) {
        check_default_column_a1(live_row_one[0], "set_column explicit default StyleId live row_cells");
        check_default_column_b1(live_row_one[1], "set_column explicit default StyleId live row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_two = sheet.row_cells(2);
    check(live_row_two.size() == 2,
        "set_column explicit default StyleId live row_cells should expose second row");
    if (live_row_two.size() == 2) {
        check_default_column_a2(live_row_two[0], "set_column explicit default StyleId live row_cells");
        check_default_column_b2(live_row_two[1], "set_column explicit default StyleId live row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_three = sheet.row_cells(3);
    check(live_row_three.size() == 1,
        "set_column explicit default StyleId live row_cells should expose inserted blank row");
    if (live_row_three.size() == 1) {
        check_default_column_a3(live_row_three[0], "set_column explicit default StyleId live row_cells");
    }
    check(sheet.row_cells(4).empty(),
        "set_column explicit default StyleId live row_cells should keep missing row empty");

    const auto inspect_default_column_output =
        [check_default_column_a1, check_default_column_b1,
            check_default_column_a2, check_default_column_b2,
            check_default_column_a3, non_default_style](
            fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 5,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                prefix + " reopened output should keep column bounds");
            check(reopened_sheet.contains_cell("A1") &&
                    reopened_sheet.contains_cell("A2") &&
                    reopened_sheet.contains_cell("A3") &&
                    reopened_sheet.contains_cell("B1") &&
                    reopened_sheet.contains_cell("B2"),
                prefix + " reopened output should keep represented cells queryable");
            check(!reopened_sheet.contains_cell("A4") &&
                    !reopened_sheet.contains_cell("D4"),
                prefix + " reopened output should keep unrelated missing cells absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " reopened sparse_cells should expose five records");
            if (cells.size() == 5) {
                check_default_column_a1(cells[0], prefix + " reopened sparse_cells");
                check_default_column_b1(cells[1], prefix + " reopened sparse_cells");
                check_default_column_a2(cells[2], prefix + " reopened sparse_cells");
                check_default_column_b2(cells[3], prefix + " reopened sparse_cells");
                check_default_column_a3(cells[4], prefix + " reopened sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 3,
                prefix + " reopened column_cells should expose replaced column");
            if (column_one.size() == 3) {
                check_default_column_a1(column_one[0], prefix + " reopened column_cells");
                check_default_column_a2(column_one[1], prefix + " reopened column_cells");
                check_default_column_a3(column_one[2], prefix + " reopened column_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 2,
                prefix + " reopened column_cells should expose non-target column");
            if (column_two.size() == 2) {
                check_default_column_b1(column_two[0], prefix + " reopened column_cells");
                check_default_column_b2(column_two[1], prefix + " reopened column_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " reopened row_cells should expose first row");
            if (row_one.size() == 2) {
                check_default_column_a1(row_one[0], prefix + " reopened row_cells");
                check_default_column_b1(row_one[1], prefix + " reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 2,
                prefix + " reopened row_cells should expose second row");
            if (row_two.size() == 2) {
                check_default_column_a2(row_two[0], prefix + " reopened row_cells");
                check_default_column_b2(row_two[1], prefix + " reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 1,
                prefix + " reopened row_cells should expose inserted blank row");
            if (row_three.size() == 1) {
                check_default_column_a3(row_three[0], prefix + " reopened row_cells");
            }
            check(reopened_sheet.row_cells(4).empty(),
                prefix + " reopened row_cells should keep missing row empty");

            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "column-default-text" &&
                    !reopened_a1.has_style(),
                prefix + " reopened output should read A1 without a style handle");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 10.0 &&
                    reopened_b1.has_style() &&
                    reopened_b1.style_id().value() == non_default_style.value(),
                prefix + " reopened output should preserve non-target B1 style");
            check(!reopened_sheet.try_cell("A4").has_value(),
                prefix + " reopened output should keep old target tail absent");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column explicit default StyleId save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_column explicit default StyleId save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "set_column explicit default StyleId should persist column bounds");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" t="inlineStr"><is><t>column-default-text</t></is></c>)",
        "set_column explicit default StyleId should persist A1 without a style id");
    check_contains(worksheet_xml, R"(<c r="A2"><f>A1+B1</f></c>)",
        "set_column explicit default StyleId should persist A2 without a style id");
    check_contains(worksheet_xml, R"(<c r="A3"/>)",
        "set_column explicit default StyleId should persist A3 without a style id");
    check_contains(worksheet_xml, R"(<c r="B1" s=")" +
            std::to_string(non_default_style.value()) + R"("><v>10</v></c>)",
        "set_column explicit default StyleId should preserve non-target styled B1");
    check_not_contains(worksheet_xml, R"(<c r="A1" s=")",
        "set_column explicit default StyleId should not keep the old source style on A1");
    check_not_contains(worksheet_xml, R"(<c r="A2" s=")",
        "set_column explicit default StyleId should not write a default style on A2");
    check_not_contains(worksheet_xml, R"(<c r="A3" s=")",
        "set_column explicit default StyleId should not write a default style on A3");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_column explicit default StyleId should not write default style ids");
    check_not_contains(worksheet_xml, "set-column-default-target-tail",
        "set_column explicit default StyleId should replace prior target-column tail");
    check_not_contains(worksheet_xml, R"(r="A4")",
        "set_column explicit default StyleId should omit old target-column tail rows");
    check_reopened_clean_sheet_output(
        output, "Styled", "set_column explicit default StyleId",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_column_output(
                reopened_sheet, "set_column explicit default StyleId");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_column explicit default StyleId no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_column explicit default StyleId no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column explicit default StyleId no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_column explicit default StyleId no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column explicit default StyleId no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column explicit default StyleId no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_column explicit default StyleId no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_column explicit default StyleId no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_column explicit default StyleId no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column explicit default StyleId no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_column explicit default StyleId no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_column_output(
                reopened_sheet, "set_column explicit default StyleId no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "set_column explicit default StyleId second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_column explicit default StyleId second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column explicit default StyleId second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_column explicit default StyleId second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column explicit default StyleId second no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column explicit default StyleId second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "set_column explicit default StyleId second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "set_column explicit default StyleId second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "set_column explicit default StyleId second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_column explicit default StyleId second no-op save should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_column explicit default StyleId second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column explicit default StyleId second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled",
        "set_column explicit default StyleId second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_column_output(
                reopened_sheet,
                "set_column explicit default StyleId second no-op save");
        });

    sheet.set_column(1, {
        fastxlsx::CellValue::formula("B1+B2").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::text("column-default-post-noop")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::boolean(true).with_style(fastxlsx::StyleId {}),
    });
    const fastxlsx::CellValue post_noop_live_a1 = sheet.get_cell("A1");
    check(post_noop_live_a1.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_live_a1.text_value() == "B1+B2" &&
            !post_noop_live_a1.has_style(),
        "set_column explicit default StyleId post-noop edit should keep A1 formula unstyled");
    const fastxlsx::CellValue post_noop_live_a4 = sheet.get_cell("A4");
    check(post_noop_live_a4.kind() == fastxlsx::CellValueKind::Boolean &&
            post_noop_live_a4.boolean_value() &&
            !post_noop_live_a4.has_style(),
        "set_column explicit default StyleId post-noop edit should keep A4 boolean unstyled");
    const fastxlsx::CellValue post_noop_live_b1 = sheet.get_cell("B1");
    check(post_noop_live_b1.kind() == fastxlsx::CellValueKind::Number &&
            post_noop_live_b1.number_value() == 10.0 &&
            post_noop_live_b1.has_style() &&
            post_noop_live_b1.style_id().value() == non_default_style.value(),
        "set_column explicit default StyleId post-noop edit should preserve non-target styled B1");
    check(sheet.cell_count() == 6,
        "set_column explicit default StyleId post-noop edit should expand sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 4, 2,
        "set_column explicit default StyleId post-noop edit should expand column bounds");
    check(sheet.has_pending_changes(),
        "set_column explicit default StyleId post-noop edit should dirty the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_column explicit default StyleId post-noop edit should not record a handoff before save");
    check(editor.pending_materialized_cell_count() == 6,
        "set_column explicit default StyleId post-noop edit should expose dirty sparse count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "set_column explicit default StyleId post-noop edit dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_column explicit default StyleId post-noop edit should keep diagnostics clear");

    const auto check_post_noop_column_a1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "B1+B2" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A1 formula");
        };
    const auto check_post_noop_column_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "column-default-post-noop" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A2 text");
        };
    const auto check_post_noop_column_a3 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A3 blank");
        };
    const auto check_post_noop_column_a4 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 4 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Boolean &&
                    snapshot.value.boolean_value() &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A4 boolean");
        };

    const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_column_one =
        sheet.column_cells(1);
    check(post_noop_column_one.size() == 4,
        "set_column explicit default StyleId post-noop edit column_cells should expose replaced column");
    if (post_noop_column_one.size() == 4) {
        check_post_noop_column_a1(post_noop_column_one[0],
            "set_column explicit default StyleId post-noop edit column_cells");
        check_post_noop_column_a2(post_noop_column_one[1],
            "set_column explicit default StyleId post-noop edit column_cells");
        check_post_noop_column_a3(post_noop_column_one[2],
            "set_column explicit default StyleId post-noop edit column_cells");
        check_post_noop_column_a4(post_noop_column_one[3],
            "set_column explicit default StyleId post-noop edit column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_column_two =
        sheet.column_cells(2);
    check(post_noop_column_two.size() == 2,
        "set_column explicit default StyleId post-noop edit column_cells should keep non-target column");
    if (post_noop_column_two.size() == 2) {
        check_default_column_b1(post_noop_column_two[0],
            "set_column explicit default StyleId post-noop edit column_cells");
        check_default_column_b2(post_noop_column_two[1],
            "set_column explicit default StyleId post-noop edit column_cells");
    }

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "set_column explicit default StyleId post-noop save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "set_column explicit default StyleId post-noop save should record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column explicit default StyleId post-noop save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_column explicit default StyleId post-noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column explicit default StyleId post-noop save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column explicit default StyleId post-noop save should keep diagnostics clear");

    const auto post_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column explicit default StyleId post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_column explicit default StyleId post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_column explicit default StyleId post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "set_column explicit default StyleId post-noop save should leave the second no-op output unchanged");
    check(post_noop_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_column explicit default StyleId post-noop save should preserve source styles.xml bytes");

    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:B4"/>)",
        "set_column explicit default StyleId post-noop save should expand column bounds");
    check_contains(post_noop_worksheet_xml, R"(<c r="A1"><f>B1+B2</f></c>)",
        "set_column explicit default StyleId post-noop save should persist A1 formula without a style id");
    check_contains(post_noop_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>column-default-post-noop</t></is></c>)",
        "set_column explicit default StyleId post-noop save should persist A2 without a style id");
    check_contains(post_noop_worksheet_xml, R"(<c r="A3"/>)",
        "set_column explicit default StyleId post-noop save should persist A3 without a style id");
    check_contains(post_noop_worksheet_xml, R"(<c r="A4" t="b"><v>1</v></c>)",
        "set_column explicit default StyleId post-noop save should persist A4 without a style id");
    check_contains(post_noop_worksheet_xml, R"(<c r="B1" s=")" +
            std::to_string(non_default_style.value()) + R"("><v>10</v></c>)",
        "set_column explicit default StyleId post-noop save should preserve non-target styled B1");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="A1" s=")",
        "set_column explicit default StyleId post-noop save should not revive the old source style on A1");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="A2" s=")",
        "set_column explicit default StyleId post-noop save should not write a default style on A2");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="A3" s=")",
        "set_column explicit default StyleId post-noop save should not write a default style on A3");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="A4" s=")",
        "set_column explicit default StyleId post-noop save should not write a default style on A4");
    check_not_contains(post_noop_worksheet_xml, R"(s="0")",
        "set_column explicit default StyleId post-noop save should not write default style ids");
    check_not_contains(post_noop_worksheet_xml, "column-default-text",
        "set_column explicit default StyleId post-noop save should replace the earlier A1 text");

    check_reopened_clean_sheet_output(
        post_noop_output, "Styled",
        "set_column explicit default StyleId post-noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "set_column explicit default StyleId post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 2,
                "set_column explicit default StyleId post-noop reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 4,
                "set_column explicit default StyleId post-noop reopened column_cells should expose column one");
            if (column_one.size() == 4) {
                check_post_noop_column_a1(column_one[0],
                    "set_column explicit default StyleId post-noop reopened column_cells");
                check_post_noop_column_a2(column_one[1],
                    "set_column explicit default StyleId post-noop reopened column_cells");
                check_post_noop_column_a3(column_one[2],
                    "set_column explicit default StyleId post-noop reopened column_cells");
                check_post_noop_column_a4(column_one[3],
                    "set_column explicit default StyleId post-noop reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 2,
                "set_column explicit default StyleId post-noop reopened column_cells should expose column two");
            if (column_two.size() == 2) {
                check_default_column_b1(column_two[0],
                    "set_column explicit default StyleId post-noop reopened column_cells");
                check_default_column_b2(column_two[1],
                    "set_column explicit default StyleId post-noop reopened column_cells");
            }
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_a1.text_value() == "B1+B2" &&
                    !reopened_a1.has_style(),
                "set_column explicit default StyleId post-noop reopened output should read A1 formula without a style handle");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 10.0 &&
                    reopened_b1.has_style() &&
                    reopened_b1.style_id().value() == non_default_style.value(),
                "set_column explicit default StyleId post-noop reopened output should preserve non-target B1 style");
        });
}

void test_public_worksheet_editor_set_column_style_rejection_preserves_dirty_session()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-style-rejection-dirty-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-style-rejection-dirty-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-style-rejection-dirty-noop-output.xlsx");
    const std::filesystem::path recovery_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-style-rejection-dirty-recovery-output.xlsx");
    const std::filesystem::path recovery_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-style-rejection-dirty-recovery-noop-output.xlsx");

    fastxlsx::StyleId source_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        source_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(source_style),
            fastxlsx::CellView::text("set-column-dirty-source-b1"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::text("set-column-dirty-source-a2"),
        });
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_column dirty style rejection source fixture should start with styled A1");

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
    const auto check_source_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-column-dirty-source-a2" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled source A2");
        };
    const auto check_dirty_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-column-dirty-kept" &&
                    !snapshot.value.has_style(),
                prefix + " should expose preserved dirty B1");
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
    const auto check_recovered_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-column-dirty-recovered" &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered dirty B1");
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
    const auto check_recovered_b3 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Boolean &&
                    snapshot.value.boolean_value() &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered dirty B3 boolean");
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
            check(!current_sheet.contains_cell("C1") &&
                    !current_sheet.contains_cell("B3"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 4,
                prefix + " sparse_cells should expose four represented records");
            if (cells.size() == 4) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_dirty_b1(cells[1], prefix + " sparse_cells");
                check_source_a2(cells[2], prefix + " sparse_cells");
                check_dirty_b2(cells[3], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                current_sheet.column_cells(2);
            check(column_two.size() == 2,
                prefix + " column_cells should expose dirty column two");
            if (column_two.size() == 2) {
                check_dirty_b1(column_two[0], prefix + " column_cells");
                check_dirty_b2(column_two[1], prefix + " column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                current_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " row_cells should expose row one");
            if (row_one.size() == 2) {
                check_styled_a1(row_one[0], prefix + " row_cells");
                check_dirty_b1(row_one[1], prefix + " row_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.0 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve source-styled A1");
            const fastxlsx::CellValue b1 = current_sheet.get_cell("B1");
            check(b1.kind() == fastxlsx::CellValueKind::Text &&
                    b1.text_value() == "set-column-dirty-kept" &&
                    !b1.has_style(),
                prefix + " get_cell should preserve dirty B1 without a style");
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
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 3, 2,
                prefix + " should keep the represented bounds");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2") &&
                    current_sheet.contains_cell("B2") &&
                    current_sheet.contains_cell("B3"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("C1") &&
                    !current_sheet.contains_cell("A3"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " sparse_cells should expose five represented records");
            if (cells.size() == 5) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_recovered_b1(cells[1], prefix + " sparse_cells");
                check_source_a2(cells[2], prefix + " sparse_cells");
                check_recovered_b2(cells[3], prefix + " sparse_cells");
                check_recovered_b3(cells[4], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                current_sheet.column_cells(2);
            check(column_two.size() == 3,
                prefix + " column_cells should expose recovered column two");
            if (column_two.size() == 3) {
                check_recovered_b1(column_two[0], prefix + " column_cells");
                check_recovered_b2(column_two[1], prefix + " column_cells");
                check_recovered_b3(column_two[2], prefix + " column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                current_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " row_cells should expose row one");
            if (row_one.size() == 2) {
                check_styled_a1(row_one[0], prefix + " row_cells");
                check_recovered_b1(row_one[1], prefix + " row_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.0 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve source-styled A1");
            const fastxlsx::CellValue b1 = current_sheet.get_cell("B1");
            check(b1.kind() == fastxlsx::CellValueKind::Text &&
                    b1.text_value() == "set-column-dirty-recovered" &&
                    !b1.has_style(),
                prefix + " get_cell should preserve recovered B1 without a style");
            const fastxlsx::CellValue b2 = current_sheet.get_cell("B2");
            check(b2.kind() == fastxlsx::CellValueKind::Blank &&
                    !b2.has_style(),
                prefix + " get_cell should preserve recovered B2 without a style");
            const fastxlsx::CellValue b3 = current_sheet.get_cell("B3");
            check(b3.kind() == fastxlsx::CellValueKind::Boolean &&
                    b3.boolean_value() &&
                    !b3.has_style(),
                prefix + " get_cell should preserve recovered B3 without a style");
        };

    sheet.set_column(2, {
        fastxlsx::CellValue::text("set-column-dirty-kept").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A1").with_style(fastxlsx::StyleId {}),
    });
    check(!editor.last_edit_error().has_value(),
        "set_column dirty style rejection setup should start diagnostic-clean");
    check(sheet.has_pending_changes(),
        "set_column dirty style rejection setup should dirty the materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_column dirty style rejection setup");
    check_dirty_views(sheet, "set_column dirty style rejection setup");

    bool failed = false;
    try {
        sheet.set_column(3, {
            fastxlsx::CellValue::text("set-column-dirty-rejected")
                .with_style(source_style),
        });
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "StyleId",
            "set_column dirty style rejection should expose the unsupported StyleId boundary");
    }
    check(failed,
        "set_column dirty style rejection should reject caller-supplied non-default StyleId values");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_column dirty style rejection should retain the public StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "set_column dirty style rejection should keep the prior dirty materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_column dirty style rejection",
        editor.last_edit_error());
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column dirty style rejection should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_column dirty style rejection live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_save, "set_column dirty style rejection save");
    check(!sheet.has_pending_changes(),
        "set_column dirty style rejection save should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "set_column dirty style rejection save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_column dirty style rejection save should clear dirty materialized diagnostics");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_column dirty style rejection save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column dirty style rejection save should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_column dirty style rejection saved handle");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_column dirty style rejection save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "set-column-dirty-kept",
        "set_column dirty style rejection save should persist prior dirty B1");
    check_contains(worksheet_xml, R"(<c r="B2"><f>A1</f></c>)",
        "set_column dirty style rejection save should persist prior dirty B2 formula");
    check_not_contains(worksheet_xml, "set-column-dirty-rejected",
        "set_column dirty style rejection save should not leak rejected payloads");
    check_contains(worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_column dirty style rejection save should keep source A1 styled");
    check_not_contains(worksheet_xml, R"(<c r="B1" s=")",
        "set_column dirty style rejection save should keep dirty B1 unstyled");
    check_not_contains(worksheet_xml, R"(<c r="B2" s=")",
        "set_column dirty style rejection save should keep dirty B2 unstyled");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_column dirty style rejection save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column dirty style rejection save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        output, "Styled", "set_column dirty style rejection save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "set_column dirty style rejection save");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_column dirty style rejection noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_column dirty style rejection noop save");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_column dirty style rejection noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "set_column dirty style rejection noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_column dirty style rejection noop save should keep dirty diagnostics clear");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_column dirty style rejection noop save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column dirty style rejection noop save should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_column dirty style rejection noop saved handle");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_column dirty style rejection noop output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column dirty style rejection noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_column dirty style rejection noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "set_column dirty style rejection noop save");
        });

    sheet.set_column(2, {
        fastxlsx::CellValue::text("set-column-dirty-recovered")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::boolean(true).with_style(fastxlsx::StyleId {}),
    });
    check(!editor.last_edit_error().has_value(),
        "set_column dirty style rejection recovery should clear the retained StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "set_column dirty style rejection recovery should dirty the materialized sheet again");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "set_column dirty style rejection recovery");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column dirty style rejection recovery should not queue replacement diagnostics");
    check_recovery_views(sheet, "set_column dirty style rejection recovery live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_recovery_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(recovery_output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_save,
        "set_column dirty style rejection recovery save");
    check(!sheet.has_pending_changes(),
        "set_column dirty style rejection recovery save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "set_column dirty style rejection recovery save should record one more materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_column dirty style rejection recovery save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column dirty style rejection recovery save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_column dirty style rejection recovery save should not queue replacement diagnostics");
    check_recovery_views(sheet, "set_column dirty style rejection recovery saved handle");

    const auto recovery_entries = fastxlsx::test::read_zip_entries(recovery_output);
    check(recovery_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_column dirty style rejection recovery save should preserve source styles.xml bytes");
    const std::string recovery_worksheet_xml =
        recovery_entries.at("xl/worksheets/sheet1.xml");
    check_contains(recovery_worksheet_xml, "set-column-dirty-source-a2",
        "set_column dirty style rejection recovery save should preserve source A2");
    check_contains(recovery_worksheet_xml, "set-column-dirty-recovered",
        "set_column dirty style rejection recovery save should persist recovered B1");
    check_contains(recovery_worksheet_xml, R"(<c r="B2"/>)",
        "set_column dirty style rejection recovery save should persist recovered B2 blank");
    check_contains(recovery_worksheet_xml, R"(<c r="B3" t="b"><v>1</v></c>)",
        "set_column dirty style rejection recovery save should persist recovered B3 boolean");
    check_not_contains(recovery_worksheet_xml, "set-column-dirty-kept",
        "set_column dirty style rejection recovery save should replace prior dirty B1");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B2"><f>A1</f></c>)",
        "set_column dirty style rejection recovery save should replace prior dirty B2 formula");
    check_not_contains(recovery_worksheet_xml, "set-column-dirty-source-b1",
        "set_column dirty style rejection recovery save should not revive source B1");
    check_not_contains(recovery_worksheet_xml, "set-column-dirty-rejected",
        "set_column dirty style rejection recovery save should not leak rejected payloads");
    check_contains(recovery_worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_column dirty style rejection recovery save should keep source A1 styled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B1" s=")",
        "set_column dirty style rejection recovery save should keep recovered B1 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B2" s=")",
        "set_column dirty style rejection recovery save should keep recovered B2 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B3" s=")",
        "set_column dirty style rejection recovery save should keep recovered B3 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(s="0")",
        "set_column dirty style rejection recovery save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column dirty style rejection recovery save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_output, "Styled", "set_column dirty style rejection recovery save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "set_column dirty style rejection recovery save");
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
        "set_column dirty style rejection recovery noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_noop,
        "set_column dirty style rejection recovery noop save");
    check(editor.pending_change_count() == pending_count_after_recovery_save,
        "set_column dirty style rejection recovery noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "set_column dirty style rejection recovery noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_column dirty style rejection recovery noop save should keep dirty diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "set_column dirty style rejection recovery noop save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_column dirty style rejection recovery noop save should not queue replacement diagnostics");
    check_recovery_views(
        sheet, "set_column dirty style rejection recovery noop saved handle");
    const auto recovery_noop_entries =
        fastxlsx::test::read_zip_entries(recovery_noop_output);
    check(recovery_noop_entries == recovery_entries,
        "set_column dirty style rejection recovery noop output should match the recovered output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column dirty style rejection recovery noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_noop_output, "Styled",
        "set_column dirty style rejection recovery noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "set_column dirty style rejection recovery noop save");
        });
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_set_column_replaces_sparse_column();
        test_public_worksheet_editor_set_column_replacement_drops_source_styles();
        test_public_worksheet_editor_set_column_accepts_default_style_id_as_unstyled();
        test_public_worksheet_editor_set_column_style_rejection_preserves_dirty_session();
        std::cout << "WorkbookEditor public-state set column tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state set column test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
