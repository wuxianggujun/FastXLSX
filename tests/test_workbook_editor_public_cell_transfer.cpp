#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

struct CopyCellsSource {
    std::filesystem::path path;
    fastxlsx::StyleId formula_style;
};

CopyCellsSource write_copy_cells_source(std::string_view name)
{
    CopyCellsSource source;
    source.path = fastxlsx::test::artifact_path(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source.path);
    source.formula_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.append_row({fastxlsx::CellView::number(10.0),
        fastxlsx::CellView::formula("A1+$A$1+A$1+$A1")
            .with_style(source.formula_style)});
    sheet.append_row({fastxlsx::CellView::text("source-a2")});
    sheet.append_row({});
    sheet.append_row({fastxlsx::CellView::text("row4-a"),
        fastxlsx::CellView::text("row4-b"), fastxlsx::CellView::text("row4-c"),
        fastxlsx::CellView::text("target-d4")});
    writer.close();
    return source;
}

CopyCellsSource write_cross_sheet_copy_source(std::string_view name)
{
    CopyCellsSource source;
    source.path = fastxlsx::test::artifact_path(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source.path);
    source.formula_style = writer.add_style(fastxlsx::CellStyle {"0.00"});

    fastxlsx::WorksheetWriter source_sheet = writer.add_worksheet("Source");
    source_sheet.append_row({fastxlsx::CellView::number(10.0),
        fastxlsx::CellView::formula("A1+$A$1+A$1+$A1")
            .with_style(source.formula_style)});
    source_sheet.append_row({fastxlsx::CellView::text("source-a2")});

    fastxlsx::WorksheetWriter destination_sheet = writer.add_worksheet("Destination");
    destination_sheet.append_row({fastxlsx::CellView::text("destination-a1")});
    destination_sheet.append_row({fastxlsx::CellView::text("destination-a2"),
        fastxlsx::CellView::text("old-b2").with_style(source.formula_style),
        fastxlsx::CellView::text("old-c2")});
    destination_sheet.append_row({fastxlsx::CellView::text("destination-a3"),
        fastxlsx::CellView::text("old-b3"), fastxlsx::CellView::text("gap-c3")});

    writer.close();
    return source;
}

void check_formula(const fastxlsx::CellValue& value, std::string_view formula,
    fastxlsx::StyleId style, const char* message)
{
    check(value.kind() == fastxlsx::CellValueKind::Formula
            && value.text_value() == formula
            && value.has_style()
            && value.style_id().value() == style.value(),
        message);
}

void test_copy_cells_sparse_overlay_save_retry_and_reopen()
{
    const CopyCellsSource source =
        write_copy_cells_source("fastxlsx-workbook-editor-copy-cells-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output =
        fastxlsx::test::artifact_path("fastxlsx-workbook-editor-copy-cells-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t source_cell_count = sheet.cell_count();

    sheet.copy_cells("A1:B2", "C3");
    check(sheet.cell_count() == source_cell_count + 2,
        "copy_cells should add only represented sparse source records");
    check(sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("C3").number_value() == 10.0,
        "copy_cells should copy the source number to the target offset");
    check_formula(sheet.get_cell("D3"), "C3+$A$1+C$1+$A3", source.formula_style,
        "copy_cells should translate formula references and preserve source style");
    check(sheet.get_cell("C4").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("C4").text_value() == "source-a2",
        "copy_cells should copy the represented sparse text cell");
    check(sheet.get_cell("D4").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("D4").text_value() == "target-d4",
        "copy_cells should leave target cells under source gaps unchanged");
    check(sheet.has_pending_changes() && editor.has_unsaved_changes(),
        "copy_cells should dirty the materialized session and save watermark");

    fastxlsx::WorkbookEditor moved = std::move(editor);
    check(throws_fastxlsx_error([&] { (void)sheet.has_pending_changes(); }),
        "copy_cells borrowed handle should be invalid after owner move");
    fastxlsx::WorksheetEditor moved_sheet = moved.worksheet("Data");
    check_formula(moved_sheet.get_cell("D3"), "C3+$A$1+C$1+$A3",
        source.formula_style,
        "copy_cells state should survive WorkbookEditor ownership transfer");

    check(throws_fastxlsx_error(
              [&] { moved.save_as(fastxlsx::test::artifact_dir()); }),
        "copy_cells save to a directory should fail after staging");
    check(moved_sheet.has_pending_changes() && moved.has_unsaved_changes(),
        "failed copy_cells save should preserve dirty and unsaved state");
    check_formula(moved_sheet.get_cell("D3"), "C3+$A$1+C$1+$A3",
        source.formula_style,
        "failed copy_cells save should preserve current formula projection");
    check(!moved.last_edit_error().has_value(),
        "failed save_as should not overwrite copy_cells edit diagnostics");

    moved.save_as(output);
    check(!moved_sheet.has_pending_changes() && !moved.has_unsaved_changes(),
        "successful copy_cells save should clear dirty and unsaved state");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "copy_cells save_as should leave the source package unchanged");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(reopened_sheet.get_cell("C3").number_value() == 10.0,
        "reopened copy_cells output should contain the copied number");
    check_formula(reopened_sheet.get_cell("D3"), "C3+$A$1+C$1+$A3",
        source.formula_style,
        "reopened copy_cells output should preserve translated styled formula");
    check(reopened_sheet.get_cell("C4").text_value() == "source-a2",
        "reopened copy_cells output should contain the copied text");
    check(reopened_sheet.get_cell("D4").text_value() == "target-d4",
        "reopened copy_cells output should preserve sparse target gaps");
}

void test_copy_cells_overlap_reads_snapshot()
{
    const CopyCellsSource source =
        write_copy_cells_source("fastxlsx-workbook-editor-copy-cells-overlap-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.copy_cells("A1:B1", "B1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("B1").number_value() == 10.0,
        "overlapping copy_cells should copy the original first source cell");
    check_formula(sheet.get_cell("C1"), "B1+$A$1+B$1+$A1", source.formula_style,
        "overlapping copy_cells should translate the original formula snapshot");
}

void test_copy_cells_failures_preserve_clean_state()
{
    const CopyCellsSource source =
        write_copy_cells_source("fastxlsx-workbook-editor-copy-cells-guard-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    const std::size_t source_cell_count = sheet.cell_count();

    check(throws_fastxlsx_error(
              [&] { sheet.copy_cells("A1:B2", "XFD1048576"); }),
        "copy_cells should reject a target footprint outside Excel limits");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && sheet.cell_count() == source_cell_count
            && !sheet.contains_cell("C3"),
        "copy_cells bounds failure should preserve clean sparse state");

    check(throws_fastxlsx_error([&] { sheet.copy_cells("A1:B2", "C3"); }),
        "copy_cells should enforce the materialized max_cells guardrail");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && sheet.cell_count() == source_cell_count
            && !sheet.contains_cell("C3")
            && sheet.get_cell("D4").text_value() == "target-d4",
        "copy_cells guard failure should not publish a partial overlay");
    check(editor.last_edit_error().has_value(),
        "copy_cells mutation failure should update last_edit_error");

    sheet.copy_cells("A1:B2", "A1");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && sheet.cell_count() == source_cell_count
            && !editor.last_edit_error().has_value(),
        "same-location copy_cells should be a clean no-op and clear diagnostics");
}

void test_cross_sheet_copy_sparse_overlay_save_retry_and_reopen()
{
    const CopyCellsSource source = write_cross_sheet_copy_source(
        "fastxlsx-workbook-editor-cross-sheet-copy-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-cross-sheet-copy-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditor destination_sheet = editor.worksheet("Destination");
    const std::size_t source_cell_count = source_sheet.cell_count();
    const std::size_t destination_cell_count = destination_sheet.cell_count();

    destination_sheet.copy_cells_from(source_sheet, "A1:B2", "B2");
    check(!source_sheet.has_pending_changes()
            && source_sheet.cell_count() == source_cell_count,
        "copy_cells_from should leave the source materialized session unchanged");
    check(destination_sheet.has_pending_changes() && editor.has_unsaved_changes(),
        "copy_cells_from should dirty only the destination session");
    check(destination_sheet.cell_count() == destination_cell_count,
        "cross-sheet sparse overlay should keep count when all copied records replace targets");
    check(destination_sheet.get_cell("B2").kind() == fastxlsx::CellValueKind::Number
            && destination_sheet.get_cell("B2").number_value() == 10.0,
        "copy_cells_from should copy the source number into the destination sheet");
    check_formula(destination_sheet.get_cell("C2"),
        "B2+$A$1+B$1+$A2", source.formula_style,
        "copy_cells_from should translate formula references and preserve workbook-local style");
    check(destination_sheet.get_cell("B3").text_value() == "source-a2",
        "copy_cells_from should copy represented sparse text records");
    check(destination_sheet.get_cell("C3").text_value() == "gap-c3",
        "copy_cells_from should preserve targets under source gaps");

    check(throws_fastxlsx_error(
              [&] { editor.save_as(fastxlsx::test::artifact_dir()); }),
        "cross-sheet copy save to a directory should fail after staging");
    check(!source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && editor.has_unsaved_changes(),
        "failed cross-sheet save should preserve source-clean and destination-dirty state");
    check_formula(destination_sheet.get_cell("C2"),
        "B2+$A$1+B$1+$A2", source.formula_style,
        "failed cross-sheet save should preserve translated destination formula");

    editor.save_as(output);
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes(),
        "successful cross-sheet copy retry should clear destination dirty state");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "cross-sheet copy save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "cross-sheet copy should preserve source styles.xml bytes");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_source = reopened.worksheet("Source");
    fastxlsx::WorksheetEditor reopened_destination = reopened.worksheet("Destination");
    check(reopened_source.get_cell("A1").number_value() == 10.0
            && reopened_source.get_cell("A2").text_value() == "source-a2",
        "reopened cross-sheet output should preserve source worksheet values");
    check(reopened_destination.get_cell("B2").number_value() == 10.0
            && reopened_destination.get_cell("B3").text_value() == "source-a2"
            && reopened_destination.get_cell("C3").text_value() == "gap-c3",
        "reopened cross-sheet output should preserve sparse overlay values");
    check_formula(reopened_destination.get_cell("C2"),
        "B2+$A$1+B$1+$A2", source.formula_style,
        "reopened cross-sheet output should preserve translated styled formula");
}

void test_cross_sheet_copy_live_source_and_failures()
{
    const CopyCellsSource source = write_cross_sheet_copy_source(
        "fastxlsx-workbook-editor-cross-sheet-copy-guards.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditorOptions destination_options;
    destination_options.max_cells = 7;
    fastxlsx::WorksheetEditor destination_sheet =
        editor.worksheet("Destination", destination_options);
    const std::size_t destination_cell_count = destination_sheet.cell_count();

    check(throws_fastxlsx_error(
              [&] { destination_sheet.copy_cells_from(source_sheet, "A1:B2", "E5"); }),
        "copy_cells_from should enforce the destination max_cells guardrail");
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes()
            && destination_sheet.cell_count() == destination_cell_count
            && !destination_sheet.contains_cell("E5"),
        "copy_cells_from guard failure should preserve both sessions and clean state");
    check(editor.last_edit_error().has_value(),
        "copy_cells_from guard failure should update destination owner diagnostics");

    destination_sheet.copy_cells_from(source_sheet, "D4:E5", "A1");
    check(!destination_sheet.has_pending_changes()
            && !editor.last_edit_error().has_value(),
        "copy_cells_from empty sparse source should be a clean no-op");

    source_sheet.set_cell_value("A1", fastxlsx::CellValue::number(25.0));
    destination_sheet.copy_cells_from(source_sheet, "A1", "A1");
    check(source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && destination_sheet.get_cell("A1").number_value() == 25.0,
        "copy_cells_from should read live unsaved source state at identical cross-sheet coordinates");

    fastxlsx::WorkbookEditor other_editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor other_source = other_editor.worksheet("Source");
    const fastxlsx::CellValue before_rejected_copy = destination_sheet.get_cell("B2");
    check(throws_fastxlsx_error(
              [&] { destination_sheet.copy_cells_from(other_source, "A1", "B2"); }),
        "copy_cells_from should reject a source handle owned by another WorkbookEditor");
    const fastxlsx::CellValue after_rejected_copy = destination_sheet.get_cell("B2");
    check(after_rejected_copy.kind() == before_rejected_copy.kind()
            && after_rejected_copy.text_value() == before_rejected_copy.text_value(),
        "different-owner copy rejection should preserve destination cell state");
}

void test_value_only_copy_preserves_destination_styles()
{
    const CopyCellsSource source = write_copy_cells_source(
        "fastxlsx-workbook-editor-copy-cell-values-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t source_cell_count = sheet.cell_count();

    sheet.copy_cell_style("B1", "D4");
    sheet.copy_cell_values("A1", "D4");
    const fastxlsx::CellValue styled_number = sheet.get_cell("D4");
    check(styled_number.kind() == fastxlsx::CellValueKind::Number
            && styled_number.number_value() == 10.0
            && styled_number.has_style()
            && styled_number.style_id().value() == source.formula_style.value(),
        "copy_cell_values should replace the value while preserving target style");

    sheet.copy_cell_values("B1", "C3");
    const fastxlsx::CellValue inserted_formula = sheet.get_cell("C3");
    check(inserted_formula.kind() == fastxlsx::CellValueKind::Formula
            && inserted_formula.text_value() == "B3+$A$1+B$1+$A3"
            && !inserted_formula.has_style(),
        "copy_cell_values should translate formulas and insert missing targets unstyled");

    sheet.copy_cell_values("A1:B1", "B1");
    const fastxlsx::CellValue overlapping_number = sheet.get_cell("B1");
    check(overlapping_number.kind() == fastxlsx::CellValueKind::Number
            && overlapping_number.number_value() == 10.0
            && overlapping_number.has_style()
            && overlapping_number.style_id().value() == source.formula_style.value(),
        "overlapping value-only copy should preserve the pre-edit target style");
    const fastxlsx::CellValue overlapping_formula = sheet.get_cell("C1");
    check(overlapping_formula.kind() == fastxlsx::CellValueKind::Formula
            && overlapping_formula.text_value() == "B1+$A$1+B$1+$A1"
            && !overlapping_formula.has_style(),
        "overlapping value-only copy should read source formula snapshot and ignore source style");
    check(sheet.cell_count() == source_cell_count + 2
            && sheet.has_pending_changes()
            && editor.has_unsaved_changes(),
        "effective same-sheet value-only copies should update sparse and dirty state");
}

void test_cross_sheet_value_only_copy_save_retry_and_reopen()
{
    const CopyCellsSource source = write_cross_sheet_copy_source(
        "fastxlsx-workbook-editor-cross-sheet-value-copy-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-cross-sheet-value-copy-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditor destination_sheet = editor.worksheet("Destination");
    const std::size_t destination_cell_count = destination_sheet.cell_count();

    destination_sheet.copy_cell_values_from(source_sheet, "A1:B2", "B2");
    check(!source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && editor.has_unsaved_changes(),
        "cross-sheet value-only copy should leave source clean and dirty destination");
    check(destination_sheet.cell_count() == destination_cell_count,
        "cross-sheet value-only overlay should keep count when targets exist");
    const fastxlsx::CellValue preserved_style_number = destination_sheet.get_cell("B2");
    check(preserved_style_number.kind() == fastxlsx::CellValueKind::Number
            && preserved_style_number.number_value() == 10.0
            && preserved_style_number.has_style()
            && preserved_style_number.style_id().value() == source.formula_style.value(),
        "copy_cell_values_from should preserve an existing mapped target style");
    const fastxlsx::CellValue unstyled_formula = destination_sheet.get_cell("C2");
    check(unstyled_formula.kind() == fastxlsx::CellValueKind::Formula
            && unstyled_formula.text_value() == "B2+$A$1+B$1+$A2"
            && !unstyled_formula.has_style(),
        "copy_cell_values_from should ignore source style and preserve unstyled target state");
    check(destination_sheet.get_cell("B3").text_value() == "source-a2"
            && destination_sheet.get_cell("C3").text_value() == "gap-c3",
        "copy_cell_values_from should copy represented text and preserve source-gap target");

    check(throws_fastxlsx_error(
              [&] { editor.save_as(fastxlsx::test::artifact_dir()); }),
        "cross-sheet value copy save to a directory should fail after staging");
    check(!source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && editor.has_unsaved_changes(),
        "failed value-only save should preserve source-clean and destination-dirty state");
    check(destination_sheet.get_cell("B2").has_style()
            && !destination_sheet.get_cell("C2").has_style(),
        "failed value-only save should preserve destination style decisions");

    editor.save_as(output);
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes(),
        "successful value-only retry should clear destination dirty state");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "value-only save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "value-only copy should preserve source styles.xml bytes");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_destination = reopened.worksheet("Destination");
    const fastxlsx::CellValue reopened_number = reopened_destination.get_cell("B2");
    check(reopened_number.number_value() == 10.0
            && reopened_number.has_style()
            && reopened_number.style_id().value() == source.formula_style.value(),
        "reopened value-only output should preserve target style and copied number");
    const fastxlsx::CellValue reopened_formula = reopened_destination.get_cell("C2");
    check(reopened_formula.text_value() == "B2+$A$1+B$1+$A2"
            && !reopened_formula.has_style(),
        "reopened value-only output should preserve translated formula without source style");
}

void test_cross_sheet_value_only_copy_failures_preserve_state()
{
    const CopyCellsSource source = write_cross_sheet_copy_source(
        "fastxlsx-workbook-editor-cross-sheet-value-copy-guards.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditorOptions destination_options;
    destination_options.max_cells = 7;
    fastxlsx::WorksheetEditor destination_sheet =
        editor.worksheet("Destination", destination_options);
    const std::size_t source_cell_count = source_sheet.cell_count();
    const std::size_t destination_cell_count = destination_sheet.cell_count();

    check(throws_fastxlsx_error([&] {
        destination_sheet.copy_cell_values_from(source_sheet, "A1:B2", "E5");
    }), "copy_cell_values_from should enforce destination max_cells guardrail");
    check(source_sheet.cell_count() == source_cell_count
            && destination_sheet.cell_count() == destination_cell_count
            && destination_sheet.get_cell("B2").text_value() == "old-b2"
            && !destination_sheet.contains_cell("E5")
            && !source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes(),
        "value-only guard failure should preserve both sessions and destination styles");

    source_sheet.copy_cell_values("A1:B2", "A1");
    destination_sheet.copy_cell_values_from(source_sheet, "D4:E5", "A1");
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.last_edit_error().has_value(),
        "same-location and empty-source value-only copies should be clean no-ops");

    fastxlsx::WorkbookEditor other_editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor other_source = other_editor.worksheet("Source");
    check(throws_fastxlsx_error([&] {
        destination_sheet.copy_cell_values_from(other_source, "A1", "B2");
    }), "copy_cell_values_from should reject another WorkbookEditor owner");
    check(destination_sheet.get_cell("B2").text_value() == "old-b2"
            && destination_sheet.get_cell("B2").has_style()
            && !destination_sheet.has_pending_changes(),
        "different-owner value-only rejection should preserve destination record");
}

void test_value_only_move_preserves_both_style_snapshots()
{
    const CopyCellsSource source = write_copy_cells_source(
        "fastxlsx-workbook-editor-move-cell-values-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t source_cell_count = sheet.cell_count();

    sheet.move_cell_values("A1:B1", "B1");

    const fastxlsx::CellValue cleared_source = sheet.get_cell("A1");
    check(cleared_source.kind() == fastxlsx::CellValueKind::Blank
            && !cleared_source.has_style(),
        "move_cell_values should retain an unstyled source as an explicit blank");
    const fastxlsx::CellValue overlapping_number = sheet.get_cell("B1");
    check(overlapping_number.kind() == fastxlsx::CellValueKind::Number
            && overlapping_number.number_value() == 10.0
            && overlapping_number.has_style()
            && overlapping_number.style_id().value() == source.formula_style.value(),
        "overlapping move_cell_values should preserve the pre-edit destination style");
    const fastxlsx::CellValue moved_formula = sheet.get_cell("C1");
    check(moved_formula.kind() == fastxlsx::CellValueKind::Formula
            && moved_formula.text_value() == "B1+$A$1+B$1+$A1"
            && !moved_formula.has_style(),
        "overlapping move_cell_values should translate the source snapshot without moving its style");
    check(sheet.cell_count() == source_cell_count + 1
            && sheet.has_pending_changes()
            && editor.has_unsaved_changes(),
        "effective same-sheet value-only move should publish one candidate store");
}

void test_value_only_move_guard_failure_preserves_source_values()
{
    const CopyCellsSource source = write_copy_cells_source(
        "fastxlsx-workbook-editor-move-cell-values-guards.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 7;
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    const std::size_t source_cell_count = sheet.cell_count();

    check(throws_fastxlsx_error([&] {
        sheet.move_cell_values("A1:B2", "C3");
    }), "move_cell_values should validate the complete same-sheet candidate store");
    check(sheet.cell_count() == source_cell_count
            && sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("A2").text_value() == "source-a2"
            && !sheet.contains_cell("C3")
            && !sheet.contains_cell("D3"),
        "same-sheet value-only guard failure should publish neither source blanks nor targets");
    check(!sheet.has_pending_changes()
            && !editor.has_unsaved_changes()
            && editor.last_edit_error().has_value(),
        "same-sheet value-only guard failure should preserve clean save state");

    sheet.move_cell_values("A1:B2", "A1");
    check(!sheet.has_pending_changes()
            && !editor.has_unsaved_changes()
            && !editor.last_edit_error().has_value(),
        "same-location value-only move should be a clean no-op and clear diagnostics");
}

void test_cross_sheet_value_only_move_save_retry_and_reopen()
{
    const CopyCellsSource source = write_cross_sheet_copy_source(
        "fastxlsx-workbook-editor-cross-sheet-value-move-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-cross-sheet-value-move-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditor destination_sheet = editor.worksheet("Destination");
    const std::size_t source_cell_count = source_sheet.cell_count();
    const std::size_t destination_cell_count = destination_sheet.cell_count();

    destination_sheet.move_cell_values_from(source_sheet, "A1:B2", "B2");
    check(source_sheet.cell_count() == source_cell_count
            && source_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank
            && source_sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "move_cell_values_from should clear source values without erasing sparse records");
    const fastxlsx::CellValue cleared_styled_formula = source_sheet.get_cell("B1");
    check(cleared_styled_formula.kind() == fastxlsx::CellValueKind::Blank
            && cleared_styled_formula.has_style()
            && cleared_styled_formula.style_id().value() == source.formula_style.value(),
        "move_cell_values_from should retain the source formula cell style on its blank");
    check(source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && editor.has_unsaved_changes(),
        "effective cross-sheet value-only move should dirty both sessions");
    check(destination_sheet.cell_count() == destination_cell_count
            && destination_sheet.get_cell("B2").number_value() == 10.0,
        "move_cell_values_from should overlay source values without changing represented target count");
    const fastxlsx::CellValue styled_target = destination_sheet.get_cell("B2");
    check(styled_target.has_style()
            && styled_target.style_id().value() == source.formula_style.value(),
        "move_cell_values_from should preserve the destination number target style");
    const fastxlsx::CellValue destination_formula = destination_sheet.get_cell("C2");
    check(destination_formula.kind() == fastxlsx::CellValueKind::Formula
            && destination_formula.text_value() == "B2+$A$1+B$1+$A2"
            && !destination_formula.has_style(),
        "move_cell_values_from should translate formula text and ignore its source style");
    check(destination_sheet.get_cell("B3").text_value() == "source-a2"
            && destination_sheet.get_cell("C3").text_value() == "gap-c3",
        "move_cell_values_from should copy represented text and preserve source-gap targets");

    check(throws_fastxlsx_error(
              [&] { editor.save_as(fastxlsx::test::artifact_dir()); }),
        "cross-sheet value-only move save to a directory should fail after staging");
    check(source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && editor.has_unsaved_changes()
            && source_sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "failed value-only move save should retain both current session projections");

    editor.save_as(output);
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes(),
        "successful value-only move retry should clear both dirty sessions");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "value-only move save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "value-only move should preserve styles.xml bytes");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_source = reopened.worksheet("Source");
    fastxlsx::WorksheetEditor reopened_destination = reopened.worksheet("Destination");
    check(reopened_source.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank
            && reopened_source.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened value-only move output should preserve explicit source blanks");
    const fastxlsx::CellValue reopened_styled_blank = reopened_source.get_cell("B1");
    check(reopened_styled_blank.kind() == fastxlsx::CellValueKind::Blank
            && reopened_styled_blank.has_style()
            && reopened_styled_blank.style_id().value() == source.formula_style.value(),
        "reopened value-only move output should preserve the source blank style");
    check(reopened_destination.get_cell("B2").number_value() == 10.0
            && reopened_destination.get_cell("B3").text_value() == "source-a2",
        "reopened value-only move output should preserve destination values");
    const fastxlsx::CellValue reopened_formula = reopened_destination.get_cell("C2");
    check(reopened_formula.text_value() == "B2+$A$1+B$1+$A2"
            && !reopened_formula.has_style(),
        "reopened value-only move output should preserve translated target formula style ownership");
}

void test_cross_sheet_value_only_move_failures_preserve_both_sessions()
{
    const CopyCellsSource source = write_cross_sheet_copy_source(
        "fastxlsx-workbook-editor-cross-sheet-value-move-guards.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditorOptions destination_options;
    destination_options.max_cells = 7;
    fastxlsx::WorksheetEditor destination_sheet =
        editor.worksheet("Destination", destination_options);
    const std::size_t source_cell_count = source_sheet.cell_count();
    const std::size_t destination_cell_count = destination_sheet.cell_count();

    check(throws_fastxlsx_error([&] {
        destination_sheet.move_cell_values_from(source_sheet, "A1:B2", "E5");
    }), "move_cell_values_from should enforce destination max_cells before dual commit");
    check(source_sheet.cell_count() == source_cell_count
            && source_sheet.get_cell("A1").number_value() == 10.0
            && source_sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Formula
            && destination_sheet.cell_count() == destination_cell_count
            && destination_sheet.get_cell("B2").text_value() == "old-b2"
            && !destination_sheet.contains_cell("E5"),
        "value-only move guard failure should publish neither source blanks nor target values");
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes()
            && editor.last_edit_error().has_value(),
        "value-only move guard failure should preserve both clean sessions");

    source_sheet.move_cell_values_from(source_sheet, "A1:B2", "A1");
    destination_sheet.move_cell_values_from(source_sheet, "D4:E5", "A1");
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.last_edit_error().has_value(),
        "same-location and empty-source value-only moves should be clean no-ops");

    fastxlsx::WorkbookEditor other_editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor other_source = other_editor.worksheet("Source");
    check(throws_fastxlsx_error([&] {
        destination_sheet.move_cell_values_from(other_source, "A1", "B2");
    }), "move_cell_values_from should reject another WorkbookEditor owner");
    check(source_sheet.get_cell("A1").number_value() == 10.0
            && destination_sheet.get_cell("B2").text_value() == "old-b2"
            && !destination_sheet.has_pending_changes(),
        "different-owner value-only move rejection should preserve both local sessions");
}

void test_cross_sheet_move_dual_session_save_retry_and_reopen()
{
    const CopyCellsSource source = write_cross_sheet_copy_source(
        "fastxlsx-workbook-editor-cross-sheet-move-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-cross-sheet-move-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditor destination_sheet = editor.worksheet("Destination");
    const std::size_t destination_cell_count = destination_sheet.cell_count();

    destination_sheet.move_cells_from(source_sheet, "A1:B2", "B2");
    check(source_sheet.cell_count() == 0
            && !source_sheet.contains_cell("A1")
            && !source_sheet.contains_cell("B1")
            && !source_sheet.contains_cell("A2"),
        "move_cells_from should remove represented records from the source session");
    check(source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && editor.has_unsaved_changes(),
        "effective cross-sheet move should dirty both materialized sessions");
    check(destination_sheet.cell_count() == destination_cell_count,
        "cross-sheet move overlay should keep destination count when targets exist");
    check(destination_sheet.get_cell("B2").kind() == fastxlsx::CellValueKind::Number
            && destination_sheet.get_cell("B2").number_value() == 10.0,
        "move_cells_from should transfer the source number");
    check_formula(destination_sheet.get_cell("C2"),
        "B2+$A$1+B$1+$A2", source.formula_style,
        "move_cells_from should translate formula references and preserve style");
    check(destination_sheet.get_cell("B3").text_value() == "source-a2",
        "move_cells_from should transfer represented sparse text");
    check(destination_sheet.get_cell("C3").text_value() == "gap-c3",
        "move_cells_from should preserve targets under source gaps");

    check(throws_fastxlsx_error(
              [&] { editor.save_as(fastxlsx::test::artifact_dir()); }),
        "cross-sheet move save to a directory should fail after staging");
    check(source_sheet.has_pending_changes()
            && destination_sheet.has_pending_changes()
            && editor.has_unsaved_changes()
            && source_sheet.cell_count() == 0,
        "failed cross-sheet move save should retain both dirty session states");
    check_formula(destination_sheet.get_cell("C2"),
        "B2+$A$1+B$1+$A2", source.formula_style,
        "failed cross-sheet move save should preserve destination projection");

    editor.save_as(output);
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes(),
        "successful cross-sheet move retry should clear both session dirty states");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "cross-sheet move save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "cross-sheet move should preserve source styles.xml bytes");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_source = reopened.worksheet("Source");
    fastxlsx::WorksheetEditor reopened_destination = reopened.worksheet("Destination");
    check(reopened_source.cell_count() == 0 && !reopened_source.used_range().has_value(),
        "reopened cross-sheet move output should omit moved source records");
    check(reopened_destination.get_cell("B2").number_value() == 10.0
            && reopened_destination.get_cell("B3").text_value() == "source-a2"
            && reopened_destination.get_cell("C3").text_value() == "gap-c3",
        "reopened cross-sheet move output should preserve sparse destination overlay");
    check_formula(reopened_destination.get_cell("C2"),
        "B2+$A$1+B$1+$A2", source.formula_style,
        "reopened cross-sheet move output should preserve translated styled formula");
}

void test_cross_sheet_move_failures_preserve_both_sessions()
{
    const CopyCellsSource source = write_cross_sheet_copy_source(
        "fastxlsx-workbook-editor-cross-sheet-move-guards.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Source");
    fastxlsx::WorksheetEditorOptions destination_options;
    destination_options.max_cells = 7;
    fastxlsx::WorksheetEditor destination_sheet =
        editor.worksheet("Destination", destination_options);
    const std::size_t source_cell_count = source_sheet.cell_count();
    const std::size_t destination_cell_count = destination_sheet.cell_count();

    check(throws_fastxlsx_error([&] {
        destination_sheet.move_cells_from(source_sheet, "A1:B2", "E5");
    }), "move_cells_from should enforce destination max_cells before dual commit");
    check(source_sheet.cell_count() == source_cell_count
            && source_sheet.contains_cell("A1")
            && source_sheet.contains_cell("B1")
            && source_sheet.contains_cell("A2")
            && destination_sheet.cell_count() == destination_cell_count
            && !destination_sheet.contains_cell("E5"),
        "destination guard failure should publish neither source removal nor target overlay");
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.has_unsaved_changes()
            && editor.last_edit_error().has_value(),
        "cross-sheet move guard failure should preserve both clean sessions");

    source_sheet.move_cells_from(source_sheet, "A1:B2", "A1");
    destination_sheet.move_cells_from(source_sheet, "D4:E5", "A1");
    check(!source_sheet.has_pending_changes()
            && !destination_sheet.has_pending_changes()
            && !editor.last_edit_error().has_value(),
        "same-session same-location and empty cross-sheet moves should be clean no-ops");

    fastxlsx::WorkbookEditor other_editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor other_source = other_editor.worksheet("Source");
    check(throws_fastxlsx_error([&] {
        destination_sheet.move_cells_from(other_source, "A1", "B2");
    }), "move_cells_from should reject a source owned by another WorkbookEditor");
    check(source_sheet.cell_count() == source_cell_count
            && destination_sheet.get_cell("B2").text_value() == "old-b2"
            && !destination_sheet.has_pending_changes(),
        "different-owner move rejection should preserve local source and destination state");
}

void test_move_cells_sparse_transfer_save_retry_and_reopen()
{
    const CopyCellsSource source =
        write_copy_cells_source("fastxlsx-workbook-editor-move-cells-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output =
        fastxlsx::test::artifact_path("fastxlsx-workbook-editor-move-cells-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.move_cells("A1:B2", "C3");
    check(!sheet.contains_cell("A1") && !sheet.contains_cell("B1")
            && !sheet.contains_cell("A2"),
        "move_cells should remove represented source records");
    check(sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("C3").number_value() == 10.0,
        "move_cells should transfer the source number to the target offset");
    check_formula(sheet.get_cell("D3"), "C3+$A$1+C$1+$A3", source.formula_style,
        "move_cells should translate formula references and preserve source style");
    check(sheet.get_cell("C4").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("C4").text_value() == "source-a2",
        "move_cells should transfer the represented sparse text cell");
    check(sheet.get_cell("D4").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("D4").text_value() == "target-d4",
        "move_cells should leave target cells under source gaps unchanged");
    check(sheet.has_pending_changes() && editor.has_unsaved_changes(),
        "move_cells should dirty the materialized session and save watermark");

    check(throws_fastxlsx_error(
              [&] { editor.save_as(fastxlsx::test::artifact_dir()); }),
        "move_cells save to a directory should fail after staging");
    check(sheet.has_pending_changes() && editor.has_unsaved_changes()
            && !sheet.contains_cell("A1"),
        "failed move_cells save should preserve current transferred state");
    check_formula(sheet.get_cell("D3"), "C3+$A$1+C$1+$A3", source.formula_style,
        "failed move_cells save should preserve translated formula state");

    editor.save_as(output);
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes(),
        "successful move_cells retry should clear dirty and unsaved state");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "move_cells save_as should leave the source package unchanged");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened_sheet.contains_cell("A1") && !reopened_sheet.contains_cell("B1")
            && !reopened_sheet.contains_cell("A2"),
        "reopened move_cells output should omit moved source records");
    check(reopened_sheet.get_cell("C3").number_value() == 10.0,
        "reopened move_cells output should contain the transferred number");
    check_formula(reopened_sheet.get_cell("D3"), "C3+$A$1+C$1+$A3",
        source.formula_style,
        "reopened move_cells output should preserve translated styled formula");
    check(reopened_sheet.get_cell("C4").text_value() == "source-a2"
            && reopened_sheet.get_cell("D4").text_value() == "target-d4",
        "reopened move_cells output should preserve sparse target overlay semantics");
}

void test_move_cells_overlap_reads_snapshot()
{
    const CopyCellsSource source =
        write_copy_cells_source("fastxlsx-workbook-editor-move-cells-overlap-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.move_cells("A1:B1", "B1");
    check(!sheet.contains_cell("A1"),
        "overlapping move_cells should remove the original source coordinate");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("B1").number_value() == 10.0,
        "overlapping move_cells should transfer the original first source cell");
    check_formula(sheet.get_cell("C1"), "B1+$A$1+B$1+$A1", source.formula_style,
        "overlapping move_cells should translate the original formula snapshot");
}

void test_move_cells_failures_preserve_clean_state()
{
    const CopyCellsSource source =
        write_copy_cells_source("fastxlsx-workbook-editor-move-cells-guard-source.xlsx");
    std::size_t exact_source_memory = 0;
    {
        fastxlsx::WorkbookEditor probe = fastxlsx::WorkbookEditor::open(source.path);
        exact_source_memory = probe.worksheet("Data").estimated_memory_usage();
    }

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditorOptions options;
    options.memory_budget_bytes = exact_source_memory;
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    const std::size_t source_cell_count = sheet.cell_count();

    check(throws_fastxlsx_error(
              [&] { sheet.move_cells("A1:B2", "XFD1048576"); }),
        "move_cells should reject a target footprint outside Excel limits");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && sheet.cell_count() == source_cell_count
            && sheet.contains_cell("A1") && !sheet.contains_cell("C3"),
        "move_cells bounds failure should preserve clean sparse state");

    check(throws_fastxlsx_error(
              [&] { sheet.move_cells("A1:B2", "XFC1048575"); }),
        "move_cells should enforce memory budget after formula translation");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && sheet.cell_count() == source_cell_count
            && sheet.contains_cell("A1") && sheet.contains_cell("B1")
            && !sheet.contains_cell("XFC1048575")
            && sheet.get_cell("D4").text_value() == "target-d4",
        "move_cells memory failure should not publish source removal or target overlay");
    check(editor.last_edit_error().has_value(),
        "move_cells mutation failure should update last_edit_error");

    sheet.move_cells("A1:B2", "A1");
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && sheet.cell_count() == source_cell_count
            && !editor.last_edit_error().has_value(),
        "same-location move_cells should be a clean no-op and clear diagnostics");
}

void test_same_session_from_aliases_are_clean_noops()
{
    const CopyCellsSource source =
        write_copy_cells_source("fastxlsx-workbook-editor-same-session-transfer-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-same-session-transfer-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t baseline_cell_count = sheet.cell_count();
    const std::size_t baseline_memory = sheet.estimated_memory_usage();

    const auto check_clean_source_state = [&] {
        check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
                && !editor.last_edit_error().has_value()
                && editor.pending_change_count() == 0
                && editor.pending_materialized_worksheet_names().empty()
                && sheet.cell_count() == baseline_cell_count
                && sheet.estimated_memory_usage() == baseline_memory,
            "same-session from alias should preserve clean editor/session state");
        check(sheet.get_cell("A1").number_value() == 10.0
                && sheet.get_cell("A2").text_value() == "source-a2"
                && sheet.get_cell("D4").text_value() == "target-d4",
            "same-session from alias should preserve source sparse values");
        check_formula(sheet.get_cell("B1"), "A1+$A$1+A$1+$A1",
            source.formula_style,
            "same-session from alias should preserve formula text and style");
    };
    const auto seed_recoverable_diagnostic = [&] {
        check(throws_fastxlsx_error(
                  [&] { sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid")); }),
            "same-session alias setup should reject invalid mutation");
        check(editor.last_edit_error().has_value(),
            "same-session alias setup should retain mutation diagnostic");
    };

    seed_recoverable_diagnostic();
    sheet.copy_cells_from(sheet, "A1:B2", "A1");
    check_clean_source_state();

    seed_recoverable_diagnostic();
    sheet.copy_cell_values_from(sheet, "A1:B2", "A1");
    check_clean_source_state();

    seed_recoverable_diagnostic();
    sheet.move_cell_values_from(sheet, "A1:B2", "A1");
    check_clean_source_state();

    seed_recoverable_diagnostic();
    sheet.move_cells_from(sheet, "A1:B2", "A1");
    check_clean_source_state();

    editor.save_as(output);
    check(fastxlsx::test::read_zip_entries(output) == source_entries
            && fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "same-session aliases should preserve exact source entries on no-op save");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_unsaved_changes() && !reopened_sheet.has_pending_changes()
            && reopened_sheet.cell_count() == baseline_cell_count,
        "same-session alias output should reopen cleanly");
    check_formula(reopened_sheet.get_cell("B1"), "A1+$A$1+A$1+$A1",
        source.formula_style,
        "same-session alias output should preserve formula text and style");
}

} // namespace

int main()
{
    try {
        test_copy_cells_sparse_overlay_save_retry_and_reopen();
        test_copy_cells_overlap_reads_snapshot();
        test_copy_cells_failures_preserve_clean_state();
        test_cross_sheet_copy_sparse_overlay_save_retry_and_reopen();
        test_cross_sheet_copy_live_source_and_failures();
        test_value_only_copy_preserves_destination_styles();
        test_cross_sheet_value_only_copy_save_retry_and_reopen();
        test_cross_sheet_value_only_copy_failures_preserve_state();
        test_value_only_move_preserves_both_style_snapshots();
        test_value_only_move_guard_failure_preserves_source_values();
        test_cross_sheet_value_only_move_save_retry_and_reopen();
        test_cross_sheet_value_only_move_failures_preserve_both_sessions();
        test_cross_sheet_move_dual_session_save_retry_and_reopen();
        test_cross_sheet_move_failures_preserve_both_sessions();
        test_move_cells_sparse_transfer_save_retry_and_reopen();
        test_move_cells_overlap_reads_snapshot();
        test_move_cells_failures_preserve_clean_state();
        test_same_session_from_aliases_are_clean_noops();
        std::cout << "WorkbookEditor public cell transfer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public cell transfer test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
