#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

std::filesystem::path write_two_sheet_source(std::string_view filename)
{
    const std::filesystem::path path = artifact(filename);
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

void check_no_replacement_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(editor.pending_replacement_cell_count() == 0
            && editor.estimated_pending_replacement_memory_usage() == 0
            && editor.pending_replacement_worksheet_names().empty(),
        prefix + " should expose no whole-sheet replacement diagnostics");
    check(editor.pending_targeted_cell_replacement_count() == 0
            && editor.pending_targeted_cell_replacement_worksheet_names().empty()
            && editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0,
        prefix + " should expose no targeted replacement diagnostics");
}

void check_clean_materialized_state(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(!sheet.has_pending_changes(),
        prefix + " should keep the materialized worksheet clean");
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.pending_change_count() == 0
            && editor.unsaved_change_count() == 0,
        prefix + " should keep editor pending and unsaved state empty");
    check(editor.pending_materialized_worksheet_names().empty()
            && editor.pending_materialized_cell_count() == 0
            && editor.estimated_pending_materialized_memory_usage() == 0
            && editor.pending_worksheet_edits().empty(),
        prefix + " should expose no dirty materialized diagnostics");
    check_no_replacement_diagnostics(editor, scenario);
}

void check_dirty_materialized_state(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet, std::string_view sheet_name,
    std::size_t expected_cell_count, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.has_pending_changes() && editor.has_pending_changes()
            && editor.has_unsaved_changes(),
        prefix + " should expose dirty materialized state");
    check(editor.pending_change_count() == 0
            && editor.unsaved_change_count() == 1,
        prefix + " should not count a materialized handoff before save");
    check(editor.pending_materialized_worksheet_names()
            == std::vector<std::string> {std::string(sheet_name)}
            && editor.pending_materialized_cell_count() == expected_cell_count
            && editor.estimated_pending_materialized_memory_usage()
                == sheet.estimated_memory_usage(),
        prefix + " should expose matching materialized diagnostics");
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1 && summaries[0].planned_name == sheet_name
            && summaries[0].materialized_dirty
            && summaries[0].materialized_cell_count == expected_cell_count,
        prefix + " should expose one matching dirty worksheet summary");
    check_no_replacement_diagnostics(editor, scenario);
}

void check_saved_materialized_state(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet, std::size_t expected_handoff_count,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(!sheet.has_pending_changes(),
        prefix + " should leave the materialized worksheet clean");
    check(editor.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.pending_change_count() == expected_handoff_count
            && editor.unsaved_change_count() == 0,
        prefix + " should expose only the retained materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty()
            && editor.pending_materialized_cell_count() == 0
            && editor.estimated_pending_materialized_memory_usage() == 0
            && editor.pending_worksheet_edits().empty(),
        prefix + " should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep public diagnostics clear");
    check_no_replacement_diagnostics(editor, scenario);
}

template <typename Inspect>
void check_reopened_clean_sheet(const std::filesystem::path& path,
    std::string_view sheet_name, std::string_view scenario, Inspect&& inspect)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet(sheet_name);
    check_clean_materialized_state(editor, sheet, scenario);
    inspect(sheet);
    check_clean_materialized_state(editor, sheet, scenario);
}

void test_append_column_uses_global_sparse_max_column_and_roundtrips()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-worksheet-append-column-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-column-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-column-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.append_column({
        fastxlsx::CellValue::text("appended-c1"),
        fastxlsx::CellValue::number(7.0),
        fastxlsx::CellValue::formula("A1+C2"),
        fastxlsx::CellValue::blank(),
        fastxlsx::CellValue::error("#DIV/0!"),
    });

    check(sheet.cell_count() == 8,
        "append_column should add one sparse record for each input value");
    check_cell_range_equals(sheet.used_range(), 1, 1, 5, 3,
        "append_column should use the global maximum represented column");
    check(sheet.get_cell("C1").text_value() == "appended-c1",
        "append_column should write text to row one");
    check(sheet.get_cell("C2").number_value() == 7.0,
        "append_column should write numbers in input order");
    check(sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("C3").text_value() == "A1+C2",
        "append_column should preserve literal formula text");
    check(sheet.get_cell("C4").kind() == fastxlsx::CellValueKind::Blank,
        "append_column should represent explicit blank values");
    check(sheet.get_cell("C5").kind() == fastxlsx::CellValueKind::Error
            && sheet.get_cell("C5").text_value() == "#DIV/0!",
        "append_column should preserve error payloads");
    const auto appended_column = sheet.column_cells(3);
    check(appended_column.size() == 5
            && appended_column.front().reference.row == 1
            && appended_column.back().reference.row == 5,
        "append_column snapshot should expose ordered appended records");
    check_dirty_materialized_state(
        editor, sheet, "Data", 8, "append_column dirty state");
    check(!editor.last_edit_error().has_value(),
        "successful append_column should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C5"/>)",
        "append_column should persist the expanded sparse dimension");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>appended-c1</t></is></c>)",
        "append_column should persist appended text");
    check_contains(worksheet_xml, R"(<c r="C2"><v>7</v></c>)",
        "append_column should persist appended number");
    check_contains(worksheet_xml, R"(<c r="C3"><f>A1+C2</f></c>)",
        "append_column should persist appended formula");
    check_contains(worksheet_xml, R"(<c r="C4"/>)",
        "append_column should persist appended explicit blank");
    check_contains(worksheet_xml, R"(<c r="C5" t="e"><v>#DIV/0!</v></c>)",
        "append_column should persist appended error");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "append_column should preserve untouched worksheet XML");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append_column save should leave the source package unchanged");
    check_saved_materialized_state(editor, sheet, 1, "append_column save");

    const auto inspect_output = [](fastxlsx::WorksheetEditor& reopened) {
        check(reopened.cell_count() == 8,
            "append_column reopened output should keep sparse count");
        check_cell_range_equals(reopened.used_range(), 1, 1, 5, 3,
            "append_column reopened output should keep sparse bounds");
        check(reopened.get_cell("A2").text_value() == "placeholder-a2",
            "append_column reopened output should preserve source A2");
        check(reopened.get_cell("C1").text_value() == "appended-c1",
            "append_column reopened output should read appended text");
        check(reopened.get_cell("C2").number_value() == 7.0,
            "append_column reopened output should read appended number");
        check(reopened.get_cell("C3").kind() == fastxlsx::CellValueKind::Formula
                && reopened.get_cell("C3").text_value() == "A1+C2",
            "append_column reopened output should read appended formula");
        check(reopened.get_cell("C4").kind() == fastxlsx::CellValueKind::Blank,
            "append_column reopened output should read appended blank");
        check(reopened.get_cell("C5").kind() == fastxlsx::CellValueKind::Error
                && reopened.get_cell("C5").text_value() == "#DIV/0!",
            "append_column reopened output should read appended error");
    };
    check_reopened_clean_sheet(
        output, "Data", "append_column reopened output", inspect_output);

    editor.save_as(noop_output);
    check_saved_materialized_state(editor, sheet, 1, "append_column no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "append_column no-op output should match the first output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "append_column no-op save should leave the first output unchanged");
    check_reopened_clean_sheet(
        noop_output, "Data", "append_column no-op reopened output", inspect_output);
}

void test_append_column_empty_store_and_empty_input()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-column-empty-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-column-empty-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        (void)writer.add_worksheet("Empty");
        writer.close();
    }

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Empty");
    const std::array<fastxlsx::CellValue, 0> empty_values {};
    sheet.append_column(empty_values);
    check(sheet.cell_count() == 0 && !sheet.used_range().has_value(),
        "empty append_column input should preserve an empty sparse store");
    check_clean_materialized_state(editor, sheet, "empty append_column input");
    check(!editor.last_edit_error().has_value(),
        "empty append_column input should keep diagnostics clear");

    sheet.append_column({
        fastxlsx::CellValue::text("empty-a1"),
        fastxlsx::CellValue::boolean(true),
    });
    check(sheet.get_cell("A1").text_value() == "empty-a1"
            && sheet.get_cell("A2").boolean_value(),
        "append_column should use column A for an empty sparse store");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 1,
        "empty-store append_column should establish column-A bounds");
    check_dirty_materialized_state(
        editor, sheet, "Empty", 2, "empty-store append_column dirty state");

    editor.save_as(output);
    check_saved_materialized_state(
        editor, sheet, 1, "empty-store append_column save");
    check_reopened_clean_sheet(
        output, "Empty", "empty-store append_column reopen",
        [](fastxlsx::WorksheetEditor& reopened) {
            check(reopened.cell_count() == 2
                    && reopened.get_cell("A1").text_value() == "empty-a1"
                    && reopened.get_cell("A2").boolean_value(),
                "empty-store append_column values should roundtrip");
        });
}

void test_append_column_style_boundary()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-column-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-column-style-output.xlsx");
    fastxlsx::StyleId source_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        source_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Styled");
        sheet.append_row(
            {fastxlsx::CellView::number(1.0).with_style(source_style)});
        sheet.append_row({fastxlsx::CellView::text("source-a2")});
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");
    bool rejected = false;
    try {
        sheet.append_column(
            {fastxlsx::CellValue::number(9.0).with_style(source_style)});
    } catch (const fastxlsx::FastXlsxError& error) {
        rejected = true;
        check_contains(error.what(), "non-default StyleId",
            "append_column should diagnose caller-supplied non-default styles");
    }
    check(rejected && editor.last_edit_error().has_value(),
        "append_column should retain a public style rejection diagnostic");
    check(!sheet.contains_cell("B1") && sheet.cell_count() == 2,
        "rejected append_column style should not publish partial records");
    check_clean_materialized_state(
        editor, sheet, "append_column non-default style rejection");

    sheet.append_column({
        fastxlsx::CellValue::text("unstyled-b1").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
    });
    const fastxlsx::CellValue source_a1 = sheet.get_cell("A1");
    const fastxlsx::CellValue appended_b1 = sheet.get_cell("B1");
    const fastxlsx::CellValue appended_b2 = sheet.get_cell("B2");
    check(source_a1.has_style()
            && source_a1.style_id().value() == source_style.value(),
        "append_column should preserve existing source styles");
    check(appended_b1.text_value() == "unstyled-b1" && !appended_b1.has_style(),
        "append_column should normalize StyleId{0} and not inherit source styles");
    check(appended_b2.kind() == fastxlsx::CellValueKind::Blank
            && !appended_b2.has_style(),
        "append_column should keep appended blank cells unstyled");
    check(!editor.last_edit_error().has_value(),
        "successful append_column should clear prior style diagnostics");
    check_dirty_materialized_state(
        editor, sheet, "Styled", 4, "append_column style-boundary dirty state");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check(output_entries.at("xl/styles.xml")
            == source_entries.at("xl/styles.xml"),
        "append_column should preserve source styles.xml bytes");
    check_contains(worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value())
            + R"("><v>1</v></c>)",
        "append_column should preserve the source style id");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>unstyled-b1</t></is></c>)",
        "append_column should persist appended text without a style id");
    check_contains(worksheet_xml, R"(<c r="B2"/>)",
        "append_column should persist appended blank without a style id");
    check_not_contains(worksheet_xml, R"(r="B1" s=)",
        "append_column should not serialize a style on appended B1");
    check_not_contains(worksheet_xml, R"(r="B2" s=)",
        "append_column should not serialize a style on appended B2");
    check_saved_materialized_state(
        editor, sheet, 1, "append_column style-boundary save");

    check_reopened_clean_sheet(
        output, "Styled", "append_column style-boundary reopen",
        [source_style](fastxlsx::WorksheetEditor& reopened) {
            check(reopened.get_cell("A1").has_style()
                    && reopened.get_cell("A1").style_id().value()
                        == source_style.value(),
                "append_column reopened output should preserve source style");
            check(!reopened.get_cell("B1").has_style()
                    && !reopened.get_cell("B2").has_style(),
                "append_column reopened output should keep appended cells unstyled");
        });
}

void test_append_column_guardrails_and_column_limit()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-worksheet-append-column-guard-source.xlsx");

    {
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();
        bool rejected = false;
        try {
            sheet.append_column({fastxlsx::CellValue::text("blocked-count")});
        } catch (const fastxlsx::FastXlsxError& error) {
            rejected = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "append_column should expose max_cells diagnostics");
        }
        check(rejected && editor.last_edit_error().has_value(),
            "append_column max_cells rejection should update last_edit_error");
        check(sheet.cell_count() == 3
                && sheet.estimated_memory_usage() == baseline_memory
                && !sheet.contains_cell("C1"),
            "append_column max_cells rejection should preserve sparse state");
        check_clean_materialized_state(
            editor, sheet, "append_column max_cells rejection");

        const std::array<fastxlsx::CellValue, 0> empty_values {};
        sheet.append_column(empty_values);
        check(!editor.last_edit_error().has_value(),
            "empty append_column should clear prior guardrail diagnostics");
        check_clean_materialized_state(
            editor, sheet, "append_column empty recovery after max_cells");
    }

    std::size_t baseline_memory = 0;
    {
        fastxlsx::WorkbookEditor sizing_editor =
            fastxlsx::WorkbookEditor::open(source);
        baseline_memory =
            sizing_editor.worksheet("Data").estimated_memory_usage();
    }
    {
        fastxlsx::WorksheetEditorOptions options;
        options.memory_budget_bytes = baseline_memory;
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        bool rejected = false;
        try {
            sheet.append_column({fastxlsx::CellValue::text("blocked-memory")});
        } catch (const fastxlsx::FastXlsxError& error) {
            rejected = true;
            check_contains(error.what(),
                "CellStore memory_budget_bytes guardrail exceeded",
                "append_column should expose memory-budget diagnostics");
        }
        check(rejected && editor.last_edit_error().has_value(),
            "append_column memory rejection should update last_edit_error");
        check(sheet.cell_count() == 3
                && sheet.estimated_memory_usage() == baseline_memory
                && !sheet.contains_cell("C1"),
            "append_column memory rejection should preserve sparse state");
        check_clean_materialized_state(
            editor, sheet, "append_column memory-budget rejection");
    }

    const std::filesystem::path boundary_source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-column-xfd-source.xlsx");
    const std::filesystem::path boundary_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-append-column-xfd-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(boundary_source);
        fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Boundary");
        sheet.append_sparse_row({
            {1, fastxlsx::CellView::text("left-a1")},
            {16384, fastxlsx::CellView::text("edge-xfd1")},
        });
        writer.close();
    }
    const auto boundary_entries =
        fastxlsx::test::read_zip_entries(boundary_source);

    fastxlsx::WorkbookEditor boundary_editor =
        fastxlsx::WorkbookEditor::open(boundary_source);
    fastxlsx::WorksheetEditor boundary_sheet =
        boundary_editor.worksheet("Boundary");
    bool rejected = false;
    try {
        boundary_sheet.append_column(
            {fastxlsx::CellValue::text("past-xfd")});
    } catch (const fastxlsx::FastXlsxError& error) {
        rejected = true;
        check_contains(error.what(), "16384",
            "append_column should expose the XFD boundary diagnostic");
    }
    check(rejected && boundary_editor.last_edit_error().has_value(),
        "append_column should reject appending after represented XFD cells");
    check(boundary_sheet.cell_count() == 2
            && boundary_sheet.get_cell("XFD1").text_value() == "edge-xfd1",
        "append_column XFD rejection should preserve source sparse records");
    check_clean_materialized_state(
        boundary_editor, boundary_sheet, "append_column XFD rejection");
    boundary_editor.save_as(boundary_output);
    check(fastxlsx::test::read_zip_entries(boundary_output) == boundary_entries,
        "append_column XFD rejected save should preserve source payloads");
    check_not_contains(
        fastxlsx::test::read_zip_entries(boundary_output)
            .at("xl/worksheets/sheet1.xml"),
        "past-xfd",
        "append_column XFD rejected save should omit the rejected payload");
}

} // namespace

int main()
{
    try {
        test_append_column_uses_global_sparse_max_column_and_roundtrips();
        test_append_column_empty_store_and_empty_input();
        test_append_column_style_boundary();
        test_append_column_guardrails_and_column_limit();
        std::cout << "WorkbookEditor public-state append column tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state append column test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
