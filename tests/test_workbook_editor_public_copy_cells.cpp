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

} // namespace

int main()
{
    try {
        test_copy_cells_sparse_overlay_save_retry_and_reopen();
        test_copy_cells_overlap_reads_snapshot();
        test_copy_cells_failures_preserve_clean_state();
        std::cout << "WorkbookEditor public copy_cells tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public copy_cells test failed: " << error.what() << '\n';
        return 1;
    }
}
