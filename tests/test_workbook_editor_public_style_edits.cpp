#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

bool throws_fastxlsx_error(auto&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    }
    return false;
}

struct StyleEditSource {
    std::filesystem::path path;
    fastxlsx::StyleId decimal_style;
    fastxlsx::StyleId percent_style;
};

StyleEditSource write_style_edit_source(std::string_view name)
{
    StyleEditSource source;
    source.path = fastxlsx::test::artifact_path(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source.path);
    source.decimal_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
    source.percent_style = writer.add_style(fastxlsx::CellStyle {"0%"});
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.append_row({fastxlsx::CellView::number(12.5),
        fastxlsx::CellView::formula("A1*2").with_style(source.decimal_style),
        fastxlsx::CellView::text("styled-text").with_style(source.percent_style)});
    sheet.append_row({fastxlsx::CellView::text("unstyled-source"),
        fastxlsx::CellView::number(0.5).with_style(source.percent_style)});
    writer.close();
    return source;
}

StyleEditSource write_cross_sheet_style_source(std::string_view name)
{
    StyleEditSource source;
    source.path = fastxlsx::test::artifact_path(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source.path);
    source.decimal_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
    source.percent_style = writer.add_style(fastxlsx::CellStyle {"0%"});

    fastxlsx::WorksheetWriter source_sheet = writer.add_worksheet("Source");
    source_sheet.append_row({fastxlsx::CellView::number(10.0),
        fastxlsx::CellView::formula("A1*2").with_style(source.decimal_style)});
    source_sheet.append_row(
        {fastxlsx::CellView::text("source-percent").with_style(source.percent_style)});

    fastxlsx::WorksheetWriter destination_sheet = writer.add_worksheet("Destination");
    destination_sheet.append_row({
        fastxlsx::CellView::text("destination-a1").with_style(source.percent_style),
        fastxlsx::CellView::number(1.0).with_style(source.percent_style)});
    destination_sheet.append_row({fastxlsx::CellView::text("destination-a2"),
        fastxlsx::CellView::number(0.25).with_style(source.percent_style),
        fastxlsx::CellView::formula("A2").with_style(source.percent_style)});
    destination_sheet.append_row({fastxlsx::CellView::text("destination-a3"),
        fastxlsx::CellView::text("destination-b3").with_style(source.percent_style)});

    writer.close();
    return source;
}

void check_style(const fastxlsx::CellValue& value, fastxlsx::StyleId expected,
    const char* message)
{
    check(value.has_style() && value.style_id().value() == expected.value(), message);
}

void test_public_style_edits_save_retry_and_reopen()
{
    const StyleEditSource source =
        write_style_edit_source("fastxlsx-workbook-editor-style-edits-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output =
        fastxlsx::test::artifact_path("fastxlsx-workbook-editor-style-edits-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t source_cell_count = sheet.cell_count();

    sheet.copy_cell_style(1, 2, 1, 1);
    const fastxlsx::CellValue styled_number = sheet.get_cell("A1");
    check(styled_number.kind() == fastxlsx::CellValueKind::Number
            && styled_number.number_value() == 12.5,
        "copy_cell_style should preserve the destination number value");
    check_style(styled_number, source.decimal_style,
        "copy_cell_style should apply the represented source style");

    sheet.copy_cell_style("A2", "C1");
    const fastxlsx::CellValue unstyled_text = sheet.get_cell("C1");
    check(unstyled_text.kind() == fastxlsx::CellValueKind::Text
            && unstyled_text.text_value() == "styled-text"
            && !unstyled_text.has_style(),
        "copy_cell_style from an unstyled source should clear only the target style");

    sheet.clear_cell_style(2, 2);
    const fastxlsx::CellValue unstyled_number = sheet.get_cell("B2");
    check(unstyled_number.kind() == fastxlsx::CellValueKind::Number
            && unstyled_number.number_value() == 0.5
            && !unstyled_number.has_style(),
        "clear_cell_style should preserve the target value");
    check_style(sheet.get_cell("B1"), source.decimal_style,
        "style-only edits should leave the source cell style unchanged");
    check(sheet.cell_count() == source_cell_count,
        "style-only edits should keep the sparse cell count stable");
    check(sheet.has_pending_changes() && editor.has_unsaved_changes(),
        "style-only edits should dirty the session and save watermark");

    check(throws_fastxlsx_error(
              [&] { editor.save_as(fastxlsx::test::artifact_dir()); }),
        "style-only save to a directory should fail after staging");
    check(sheet.has_pending_changes() && editor.has_unsaved_changes(),
        "failed style-only save should preserve dirty and unsaved state");
    check_style(sheet.get_cell("A1"), source.decimal_style,
        "failed style-only save should preserve the copied style");
    check(!sheet.get_cell("C1").has_style() && !sheet.get_cell("B2").has_style(),
        "failed style-only save should preserve cleared styles");

    editor.save_as(output);
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes(),
        "successful style-only save retry should clear dirty and unsaved state");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "style-only save_as should leave the source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "style-only save should preserve source styles.xml bytes");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    const fastxlsx::CellValue reopened_number = reopened_sheet.get_cell("A1");
    check(reopened_number.number_value() == 12.5,
        "reopened style-only output should preserve destination value");
    check_style(reopened_number, source.decimal_style,
        "reopened style-only output should preserve copied style");
    check(reopened_sheet.get_cell("C1").text_value() == "styled-text"
            && !reopened_sheet.get_cell("C1").has_style(),
        "reopened style-only output should preserve copied unstyled state");
    check(reopened_sheet.get_cell("B2").number_value() == 0.5
            && !reopened_sheet.get_cell("B2").has_style(),
        "reopened style-only output should preserve explicit style clearing");
}

void test_public_style_range_edits_overlap_save_and_reopen()
{
    const StyleEditSource source =
        write_style_edit_source("fastxlsx-workbook-editor-style-ranges-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output =
        fastxlsx::test::artifact_path("fastxlsx-workbook-editor-style-ranges-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t source_cell_count = sheet.cell_count();

    sheet.copy_cell_styles("A1:B1", "B1");
    const fastxlsx::CellValue copied_unstyled_formula = sheet.get_cell("B1");
    check(copied_unstyled_formula.kind() == fastxlsx::CellValueKind::Formula
            && copied_unstyled_formula.text_value() == "A1*2"
            && !copied_unstyled_formula.has_style(),
        "overlapping copy_cell_styles should preserve the target formula and copy the original A1 style");
    const fastxlsx::CellValue copied_decimal_text = sheet.get_cell("C1");
    check(copied_decimal_text.kind() == fastxlsx::CellValueKind::Text
            && copied_decimal_text.text_value() == "styled-text",
        "copy_cell_styles should preserve the mapped target text value");
    check_style(copied_decimal_text, source.decimal_style,
        "overlapping copy_cell_styles should read B1 style from the stable source snapshot");

    sheet.clear_cell_styles(fastxlsx::CellRange {2, 2, 2, 3});
    const fastxlsx::CellValue cleared_range_number = sheet.get_cell("B2");
    check(cleared_range_number.kind() == fastxlsx::CellValueKind::Number
            && cleared_range_number.number_value() == 0.5
            && !cleared_range_number.has_style(),
        "clear_cell_styles should preserve values and ignore missing cells in the range");
    check(!sheet.contains_cell("C2") && sheet.cell_count() == source_cell_count,
        "range style edits should not synthesize sparse records");
    check(sheet.has_pending_changes() && editor.has_unsaved_changes(),
        "effective range style edits should dirty the materialized session");

    editor.save_as(output);
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes(),
        "successful range style save should clear dirty and unsaved state");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "range style save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "range style save should preserve source styles.xml bytes");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(reopened_sheet.get_cell("B1").text_value() == "A1*2"
            && !reopened_sheet.get_cell("B1").has_style(),
        "reopened range style output should preserve the mapped unstyled formula");
    check_style(reopened_sheet.get_cell("C1"), source.decimal_style,
        "reopened range style output should preserve the overlapping copied style");
    check(reopened_sheet.get_cell("B2").number_value() == 0.5
            && !reopened_sheet.get_cell("B2").has_style(),
        "reopened range style output should preserve range style clearing");
}

void test_public_cross_sheet_style_copy_save_retry_and_reopen()
{
    const StyleEditSource source = write_cross_sheet_style_source(
        "fastxlsx-workbook-editor-cross-sheet-style-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-cross-sheet-style-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditor destination_sheet = editor.worksheet("Destination");
    const std::size_t source_cell_count = source_sheet.cell_count();
    const std::size_t destination_cell_count = destination_sheet.cell_count();

    destination_sheet.copy_cell_styles_from(source_sheet, "A1:B2", "B2");
    check(!source_sheet.has_pending_changes()
            && source_sheet.cell_count() == source_cell_count,
        "copy_cell_styles_from should leave the source session unchanged");
    check(destination_sheet.has_pending_changes() && editor.has_unsaved_changes()
            && destination_sheet.cell_count() == destination_cell_count,
        "copy_cell_styles_from should dirty only the destination without changing cell count");

    const fastxlsx::CellValue cleared_number = destination_sheet.get_cell("B2");
    check(cleared_number.kind() == fastxlsx::CellValueKind::Number
            && cleared_number.number_value() == 0.25
            && !cleared_number.has_style(),
        "cross-sheet unstyled source should clear only the mapped target style");
    const fastxlsx::CellValue styled_formula = destination_sheet.get_cell("C2");
    check(styled_formula.kind() == fastxlsx::CellValueKind::Formula
            && styled_formula.text_value() == "A2",
        "copy_cell_styles_from should preserve the mapped target formula value");
    check_style(styled_formula, source.decimal_style,
        "copy_cell_styles_from should apply the source workbook-local style");
    check(destination_sheet.get_cell("B3").text_value() == "destination-b3",
        "copy_cell_styles_from should preserve mapped target text values");
    check_style(destination_sheet.get_cell("B3"), source.percent_style,
        "copy_cell_styles_from should preserve an already-equal mapped style");
    check(!destination_sheet.contains_cell("C3"),
        "copy_cell_styles_from should not synthesize targets under source gaps");

    check(throws_fastxlsx_error(
              [&] { editor.save_as(fastxlsx::test::artifact_dir()); }),
        "cross-sheet style save to a directory should fail after staging");
    check(!source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && editor.has_unsaved_changes(),
        "failed cross-sheet style save should preserve source-clean and destination-dirty state");
    check_style(destination_sheet.get_cell("C2"), source.decimal_style,
        "failed cross-sheet style save should preserve copied destination style");

    editor.save_as(output);
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes(),
        "successful cross-sheet style retry should clear destination dirty state");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "cross-sheet style save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "cross-sheet style copy should preserve source styles.xml bytes");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_source = reopened.worksheet("Source");
    fastxlsx::WorksheetEditor reopened_destination = reopened.worksheet("Destination");
    check(!reopened_source.get_cell("A1").has_style(),
        "reopened cross-sheet style output should preserve source style state");
    check(reopened_destination.get_cell("B2").number_value() == 0.25
            && !reopened_destination.get_cell("B2").has_style(),
        "reopened cross-sheet style output should preserve mapped style clearing");
    check(reopened_destination.get_cell("C2").text_value() == "A2",
        "reopened cross-sheet style output should preserve target formula value");
    check_style(reopened_destination.get_cell("C2"), source.decimal_style,
        "reopened cross-sheet style output should preserve copied style");
}

void test_public_cross_sheet_style_copy_failures_and_live_source()
{
    const StyleEditSource source = write_cross_sheet_style_source(
        "fastxlsx-workbook-editor-cross-sheet-style-guards.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditor destination_sheet = editor.worksheet("Destination");

    check(throws_fastxlsx_error([&] {
        destination_sheet.copy_cell_styles_from(source_sheet, "A1:B1", "C2");
    }), "copy_cell_styles_from should reject a missing mapped destination");
    check_style(destination_sheet.get_cell("C2"), source.percent_style,
        "missing mapped destination should not publish an earlier planned style clear");
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes()
            && editor.last_edit_error().has_value(),
        "cross-sheet style target failure should preserve both clean sessions");

    destination_sheet.copy_cell_styles_from(source_sheet, "D4:E5", "A1");
    check(!destination_sheet.has_pending_changes()
            && !editor.last_edit_error().has_value(),
        "empty cross-sheet style source should be a clean no-op");

    fastxlsx::WorkbookEditor other_editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor other_source = other_editor.worksheet("Source");
    check(throws_fastxlsx_error([&] {
        destination_sheet.copy_cell_styles_from(other_source, "B1", "B1");
    }), "copy_cell_styles_from should reject another WorkbookEditor owner");
    check_style(destination_sheet.get_cell("B1"), source.percent_style,
        "different-owner style copy rejection should preserve destination style");

    source_sheet.copy_cell_style("B1", "A1");
    destination_sheet.copy_cell_styles_from(source_sheet, "A1", "A1");
    check(source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && destination_sheet.get_cell("A1").text_value() == "destination-a1",
        "copy_cell_styles_from should preserve target value while reading live source style");
    check_style(destination_sheet.get_cell("A1"), source.decimal_style,
        "copy_cell_styles_from should apply live source style at identical cross-sheet coordinates");
}

void test_public_style_edit_failures_and_noops_preserve_clean_state()
{
    const StyleEditSource source =
        write_style_edit_source("fastxlsx-workbook-editor-style-edits-guards.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t source_cell_count = sheet.cell_count();

    check(throws_fastxlsx_error([&] { sheet.copy_cell_style("D4", "A1"); }),
        "copy_cell_style should reject a missing represented source");
    check(throws_fastxlsx_error([&] { sheet.copy_cell_style("B1", "D4"); }),
        "copy_cell_style should reject a missing represented destination");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && sheet.cell_count() == source_cell_count
            && !sheet.get_cell("A1").has_style(),
        "copy_cell_style missing-cell failures should preserve clean state");
    check(editor.last_edit_error().has_value(),
        "copy_cell_style failure should update last_edit_error");

    check(throws_fastxlsx_error(
              [&] { sheet.copy_cell_styles("B1:C1", "B2"); }),
        "copy_cell_styles should reject a missing mapped destination record");
    check_style(sheet.get_cell("B2"), source.percent_style,
        "copy_cell_styles missing-target failure should not publish earlier mapped styles");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes(),
        "copy_cell_styles missing-target failure should preserve clean state");

    sheet.copy_cell_styles("A1:C1", "A1");
    sheet.copy_cell_styles("D4:E5", "A1");
    sheet.clear_cell_styles("D4:E5");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && !editor.last_edit_error().has_value(),
        "same-footprint, empty-source, and missing-only range style edits should be clean no-ops");

    check(throws_fastxlsx_error(
              [&] { sheet.copy_cell_styles("A1:B1", "XFD1048576"); }),
        "copy_cell_styles should reject a target footprint outside Excel limits");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes(),
        "copy_cell_styles footprint failure should preserve clean state");

    sheet.copy_cell_style("B1", "B1");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && !editor.last_edit_error().has_value(),
        "same-cell copy_cell_style should be a clean no-op and clear diagnostics");

    sheet.clear_cell_style("A1");
    sheet.clear_cell_style("D4");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && sheet.cell_count() == source_cell_count,
        "clear_cell_style should no-op for unstyled and missing cells");

    check(throws_fastxlsx_error([&] { sheet.clear_cell_style("a1"); }),
        "clear_cell_style should reject invalid lowercase A1 references");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.last_edit_error().has_value(),
        "clear_cell_style parse failure should preserve clean state and record diagnostics");

    check(throws_fastxlsx_error([&] { sheet.clear_cell_styles("A1:a2"); }),
        "clear_cell_styles should reject invalid lowercase range references");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.last_edit_error().has_value(),
        "clear_cell_styles parse failure should preserve clean state and record diagnostics");
}

} // namespace

int main()
{
    try {
        test_public_style_edits_save_retry_and_reopen();
        test_public_style_range_edits_overlap_save_and_reopen();
        test_public_cross_sheet_style_copy_save_retry_and_reopen();
        test_public_cross_sheet_style_copy_failures_and_live_source();
        test_public_style_edit_failures_and_noops_preserve_clean_state();
        std::cout << "WorkbookEditor public style edit tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public style edit test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
