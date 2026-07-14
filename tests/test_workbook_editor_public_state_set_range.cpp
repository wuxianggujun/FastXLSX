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
#include <utility>
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
        data.append_row({fastxlsx::CellView::text("source-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("source-a2"),
            fastxlsx::CellView::text("source-b2"),
            fastxlsx::CellView::text("source-c2")});
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

void check_no_dirty_materialized_state(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_retained_change_count,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(!sheet.has_pending_changes(),
        prefix + " should keep the materialized worksheet clean");
    check(editor.has_pending_changes() == (expected_retained_change_count != 0)
            && !editor.has_unsaved_changes()
            && editor.pending_change_count() == expected_retained_change_count
            && editor.unsaved_change_count() == 0,
        prefix + " should expose only expected retained editor state");
    check(editor.pending_materialized_worksheet_names().empty()
            && editor.pending_materialized_cell_count() == 0
            && editor.estimated_pending_materialized_memory_usage() == 0
            && editor.pending_worksheet_edits().empty(),
        prefix + " should expose no dirty materialized diagnostics");
    check_no_replacement_diagnostics(editor, scenario);
}

void check_dirty_materialized_state(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::string_view sheet_name,
    std::size_t expected_cell_count,
    std::size_t expected_retained_change_count,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.has_pending_changes() && editor.has_pending_changes()
            && editor.has_unsaved_changes(),
        prefix + " should expose dirty materialized state");
    check(editor.pending_change_count() == expected_retained_change_count
            && editor.unsaved_change_count() == 1,
        prefix + " should expose one dirty session beyond retained changes");
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
        prefix + " should expose one matching worksheet summary");
    check_no_replacement_diagnostics(editor, scenario);
}

void check_source_data(const fastxlsx::WorksheetEditor& sheet,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.cell_count() == 5,
        prefix + " should preserve source sparse count");
    check(sheet.get_cell("A1").text_value() == "source-a1"
            && sheet.get_cell("B1").number_value() == 1.0
            && sheet.get_cell("A2").text_value() == "source-a2"
            && sheet.get_cell("B2").text_value() == "source-b2"
            && sheet.get_cell("C2").text_value() == "source-c2",
        prefix + " should preserve source values");
    check(!sheet.contains_cell("B3") && !sheet.contains_cell("C3")
            && !sheet.contains_cell("D1"),
        prefix + " should not publish rejected range cells");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        prefix + " should preserve source bounds");
}

template <typename Inspect>
void check_reopened_clean_sheet(const std::filesystem::path& path,
    std::string_view sheet_name, std::string_view scenario, Inspect&& inspect)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet(sheet_name);
    check_no_dirty_materialized_state(editor, sheet, 0, scenario);
    inspect(sheet);
    check_no_dirty_materialized_state(editor, sheet, 0, scenario);
}

void test_set_range_maps_row_major_and_roundtrips()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-worksheet-set-range-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-range-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-range-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_range("B2:C3", {
        fastxlsx::CellValue::text("range-b2"),
        fastxlsx::CellValue::number(7.0),
        fastxlsx::CellValue::formula("B2+C2"),
        fastxlsx::CellValue::blank(),
    });

    check(sheet.cell_count() == 7,
        "set_range should replace two source records and insert two records");
    check(sheet.get_cell("B2").text_value() == "range-b2",
        "set_range should map the first value to the top-left cell");
    check(sheet.get_cell("C2").number_value() == 7.0,
        "set_range should map left-to-right across the first row");
    check(sheet.get_cell("B3").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("B3").text_value() == "B2+C2",
        "set_range should map and preserve literal formula text");
    check(sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Blank,
        "set_range should map explicit blanks to the final row-major cell");
    check(sheet.get_cell("A1").text_value() == "source-a1"
            && sheet.get_cell("A2").text_value() == "source-a2",
        "set_range should preserve cells outside the target range");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 3,
        "set_range should expand sparse bounds to the dense target range");
    const auto row_three = sheet.row_cells(3);
    check(row_three.size() == 2
            && row_three[0].reference.column == 2
            && row_three[0].value.kind() == fastxlsx::CellValueKind::Formula
            && row_three[1].reference.column == 3
            && row_three[1].value.kind() == fastxlsx::CellValueKind::Blank,
        "set_range snapshots should expose row-major target records");
    check_dirty_materialized_state(
        editor, sheet, "Data", 7, 0, "set_range dirty state");
    check(!editor.last_edit_error().has_value(),
        "successful set_range should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "set_range should persist expanded bounds");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>range-b2</t></is></c>)",
        "set_range should persist row-major text");
    check_contains(worksheet_xml, R"(<c r="C2"><v>7</v></c>)",
        "set_range should persist row-major number");
    check_contains(worksheet_xml, R"(<c r="B3"><f>B2+C2</f></c>)",
        "set_range should persist literal formula text");
    check_contains(worksheet_xml, R"(<c r="C3"/>)",
        "set_range should persist explicit blank records");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "set_range should preserve untouched worksheet XML");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_range save should leave the source package unchanged");
    check_no_dirty_materialized_state(editor, sheet, 1, "set_range save");

    const auto inspect_output = [](fastxlsx::WorksheetEditor& reopened) {
        check(reopened.cell_count() == 7,
            "set_range reopened output should preserve sparse count");
        check_cell_range_equals(reopened.used_range(), 1, 1, 3, 3,
            "set_range reopened output should preserve bounds");
        check(reopened.get_cell("B2").text_value() == "range-b2"
                && reopened.get_cell("C2").number_value() == 7.0,
            "set_range reopened output should preserve first row values");
        check(reopened.get_cell("B3").kind()
                    == fastxlsx::CellValueKind::Formula
                && reopened.get_cell("B3").text_value() == "B2+C2"
                && reopened.get_cell("C3").kind()
                    == fastxlsx::CellValueKind::Blank,
            "set_range reopened output should preserve second row values");
    };
    check_reopened_clean_sheet(
        output, "Data", "set_range reopened output", inspect_output);

    const std::array<fastxlsx::CellValue, 4> equal_values {
        fastxlsx::CellValue::text("range-b2"),
        fastxlsx::CellValue::number(7.0),
        fastxlsx::CellValue::formula("B2+C2"),
        fastxlsx::CellValue::blank(),
    };
    sheet.set_range(fastxlsx::CellRange {2, 2, 3, 3}, equal_values);
    check_no_dirty_materialized_state(
        editor, sheet, 1, "equal CellRange/span set_range");
    check(!editor.last_edit_error().has_value(),
        "equal set_range should keep diagnostics clear");

    editor.save_as(noop_output);
    check_no_dirty_materialized_state(editor, sheet, 1, "set_range no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "set_range no-op output should match the first output");
    check_reopened_clean_sheet(
        noop_output, "Data", "set_range no-op reopen", inspect_output);
}

void test_set_range_style_rejection_preserves_dirty_state()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-range-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-range-style-output.xlsx");
    fastxlsx::StyleId source_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        source_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Styled");
        sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(source_style),
            fastxlsx::CellView::text("source-b1").with_style(source_style),
        });
        sheet.append_row({fastxlsx::CellView::text("source-a2")});
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");
    sheet.set_range("A2:B2", {
        fastxlsx::CellValue::text("source-a2"),
        fastxlsx::CellValue::text("dirty-b2"),
    });
    check_dirty_materialized_state(
        editor, sheet, "Styled", 4, 0, "set_range dirty style setup");

    bool rejected = false;
    try {
        sheet.set_range("A1:B1", {
            fastxlsx::CellValue::number(1.0),
            fastxlsx::CellValue::text("rejected")
                .with_style(source_style),
        });
    } catch (const fastxlsx::FastXlsxError& error) {
        rejected = true;
        check_contains(error.what(), "non-default StyleId",
            "set_range should diagnose unsupported caller styles");
    }
    check(rejected && editor.last_edit_error().has_value(),
        "set_range style rejection should update last_edit_error");
    check(sheet.get_cell("A1").has_style()
            && sheet.get_cell("B1").has_style()
            && sheet.get_cell("B1").text_value() == "source-b1"
            && sheet.get_cell("B2").text_value() == "dirty-b2",
        "set_range style rejection should preserve source styles and prior dirty cells");
    check_dirty_materialized_state(
        editor, sheet, "Styled", 4, 0, "set_range dirty style rejection");

    sheet.set_range(fastxlsx::CellRange {1, 1, 1, 2}, {
        fastxlsx::CellValue::number(1.0).with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::text("source-b1").with_style(fastxlsx::StyleId {}),
    });
    check(!sheet.get_cell("A1").has_style()
            && !sheet.get_cell("B1").has_style(),
        "set_range full replacement should drop existing target styles");
    check(sheet.get_cell("B2").text_value() == "dirty-b2",
        "successful set_range should preserve prior dirty cells outside the range");
    check(!editor.last_edit_error().has_value(),
        "successful set_range should clear prior style diagnostics");
    check_dirty_materialized_state(
        editor, sheet, "Styled", 4, 0, "set_range style recovery");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check(output_entries.at("xl/styles.xml")
            == source_entries.at("xl/styles.xml"),
        "set_range should preserve source styles.xml bytes");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "set_range should persist A1 without its former style");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>source-b1</t></is></c>)",
        "set_range should persist B1 without its former style");
    check_contains(worksheet_xml, "dirty-b2",
        "set_range should persist prior dirty state after rejection");
    check_not_contains(worksheet_xml, "rejected",
        "set_range should not leak rejected style payloads");
    check_not_contains(worksheet_xml, R"(r="A1" s=)",
        "set_range should not serialize a style on replaced A1");
    check_not_contains(worksheet_xml, R"(r="B1" s=)",
        "set_range should not serialize a style on replaced B1");
    check_no_dirty_materialized_state(
        editor, sheet, 1, "set_range style save");

    check_reopened_clean_sheet(
        output, "Styled", "set_range style reopen",
        [](fastxlsx::WorksheetEditor& reopened) {
            check(!reopened.get_cell("A1").has_style()
                    && !reopened.get_cell("B1").has_style()
                    && reopened.get_cell("B2").text_value() == "dirty-b2",
                "set_range style output should roundtrip full-cell replacements");
        });
}

template <typename Action>
void expect_clean_set_range_rejection(fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    Action&& action,
    std::string_view expected_diagnostic,
    std::string_view scenario)
{
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    bool rejected = false;
    try {
        action();
    } catch (const fastxlsx::FastXlsxError& error) {
        rejected = true;
        check_contains(error.what(), expected_diagnostic,
            std::string(scenario) + " should expose the expected diagnostic");
    }
    check(rejected && editor.last_edit_error().has_value(),
        std::string(scenario) + " should update last_edit_error");
    check_source_data(sheet, scenario);
    check(sheet.estimated_memory_usage() == baseline_memory,
        std::string(scenario) + " should preserve sparse memory");
    check_no_dirty_materialized_state(editor, sheet, 0, scenario);
}

void test_set_range_validation_and_max_coordinate()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-worksheet-set-range-validation-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-range-validation-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    expect_clean_set_range_rejection(
        editor, sheet,
        [&] {
            sheet.set_range(fastxlsx::CellRange {3, 2, 2, 3}, {
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
            });
        },
        "range", "reversed CellRange set_range");
    expect_clean_set_range_rejection(
        editor, sheet,
        [&] {
            sheet.set_range("B2:C3", {
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
            });
        },
        "value count does not match range area",
        "too-few set_range values");
    expect_clean_set_range_rejection(
        editor, sheet,
        [&] {
            sheet.set_range("B2:C3", {
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
            });
        },
        "value count does not match range area",
        "too-many set_range values");
    expect_clean_set_range_rejection(
        editor, sheet,
        [&] {
            const std::array<fastxlsx::CellValue, 0> values {};
            sheet.set_range("A1:XFD1048576", values);
        },
        "17179869184", "full-worksheet empty set_range");
    expect_clean_set_range_rejection(
        editor, sheet,
        [&] {
            sheet.set_range("b2:C3", {
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
                fastxlsx::CellValue::blank(),
            });
        },
        "cell reference is invalid", "lowercase A1 set_range");

    sheet.set_range("XFD1048576",
        {fastxlsx::CellValue::text("max-range-cell")});
    check(sheet.cell_count() == 6
            && sheet.get_cell("XFD1048576").text_value() == "max-range-cell",
        "singleton set_range should accept the maximum Excel coordinate");
    check_cell_range_equals(sheet.used_range(), 1, 1, 1048576, 16384,
        "maximum-coordinate set_range should expand sparse bounds");
    check(!editor.last_edit_error().has_value(),
        "successful maximum-coordinate set_range should clear diagnostics");
    check_dirty_materialized_state(
        editor, sheet, "Data", 6, 0, "maximum-coordinate set_range");

    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "maximum-coordinate set_range should persist expanded dimension");
    check_contains(worksheet_xml,
        R"(<c r="XFD1048576" t="inlineStr"><is><t>max-range-cell</t></is></c>)",
        "maximum-coordinate set_range should persist its singleton value");
    check_no_dirty_materialized_state(
        editor, sheet, 1, "maximum-coordinate set_range save");
    check_reopened_clean_sheet(
        output, "Data", "maximum-coordinate set_range reopen",
        [](fastxlsx::WorksheetEditor& reopened) {
            check(reopened.cell_count() == 6
                    && reopened.get_cell("XFD1048576").text_value()
                        == "max-range-cell",
                "maximum-coordinate set_range should roundtrip");
        });
}

void test_set_range_guardrails_are_atomic()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-worksheet-set-range-guard-source.xlsx");

    {
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 5;
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        expect_clean_set_range_rejection(
            editor, sheet,
            [&] {
                sheet.set_range("D1:E1", {
                    fastxlsx::CellValue::text("blocked-d1"),
                    fastxlsx::CellValue::text("blocked-e1"),
                });
            },
            "CellStore max_cells guardrail exceeded",
            "set_range max_cells rejection");
    }

    std::size_t exact_memory_budget = 0;
    {
        fastxlsx::WorkbookEditor sizing_editor =
            fastxlsx::WorkbookEditor::open(source);
        exact_memory_budget =
            sizing_editor.worksheet("Data").estimated_memory_usage();
    }
    {
        fastxlsx::WorksheetEditorOptions options;
        options.memory_budget_bytes = exact_memory_budget;
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        expect_clean_set_range_rejection(
            editor, sheet,
            [&] {
                sheet.set_range("D1",
                    {fastxlsx::CellValue::text("blocked-memory")});
            },
            "CellStore memory_budget_bytes guardrail exceeded",
            "set_range memory-budget rejection");
    }
}

void test_set_range_reacquires_after_owner_move()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-worksheet-set-range-move-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-range-move-output.xlsx");

    fastxlsx::WorkbookEditor source_editor =
        fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor stale_sheet = source_editor.worksheet("Data");
    fastxlsx::WorkbookEditor moved_editor = std::move(source_editor);

    bool stale_rejected = false;
    try {
        stale_sheet.set_range("D1",
            {fastxlsx::CellValue::text("stale-range")});
    } catch (const fastxlsx::FastXlsxError& error) {
        stale_rejected = true;
        check_contains(error.what(), "no longer attached",
            "moved-owner stale set_range should expose handle invalidation");
    }
    check(stale_rejected,
        "moved-owner stale set_range should reject the old handle");

    fastxlsx::WorksheetEditor sheet = moved_editor.worksheet("Data");
    check_no_dirty_materialized_state(
        moved_editor, sheet, 0, "set_range moved-owner reacquire");
    const std::array<fastxlsx::CellValue, 2> values {
        fastxlsx::CellValue::text("moved-d1"),
        fastxlsx::CellValue::boolean(true),
    };
    sheet.set_range("D1:E1", values);
    check(sheet.get_cell("D1").text_value() == "moved-d1"
            && sheet.get_cell("E1").boolean_value()
            && !sheet.contains_cell("F1"),
        "reacquired set_range should map span values after owner move");
    check_dirty_materialized_state(
        moved_editor, sheet, "Data", 7, 0, "set_range moved-owner edit");

    moved_editor.save_as(output);
    check_no_dirty_materialized_state(
        moved_editor, sheet, 1, "set_range moved-owner save");
    check_reopened_clean_sheet(
        output, "Data", "set_range moved-owner reopen",
        [](fastxlsx::WorksheetEditor& reopened) {
            check(reopened.get_cell("D1").text_value() == "moved-d1"
                    && reopened.get_cell("E1").boolean_value(),
                "reacquired set_range output should roundtrip");
        });
}

} // namespace

int main()
{
    try {
        test_set_range_maps_row_major_and_roundtrips();
        test_set_range_style_rejection_preserves_dirty_state();
        test_set_range_validation_and_max_coordinate();
        test_set_range_guardrails_are_atomic();
        test_set_range_reacquires_after_owner_move();
        std::cout << "WorkbookEditor public-state set range tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state set range test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
