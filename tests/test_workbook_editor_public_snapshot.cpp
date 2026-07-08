#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include "zip_test_utils.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
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

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

void check_contains(
    const std::string& haystack, std::string_view needle, const char* message)
{
    if (haystack.find(needle) == std::string::npos) {
        throw TestFailure(message);
    }
}

void check_not_contains(
    const std::string& haystack, std::string_view needle, const char* message)
{
    if (haystack.find(needle) != std::string::npos) {
        throw TestFailure(message);
    }
}

bool is_text_snapshot(
    const fastxlsx::WorksheetCellSnapshot& cell,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view text)
{
    return cell.reference.row == row &&
        cell.reference.column == column &&
        cell.value.kind() == fastxlsx::CellValueKind::Text &&
        cell.value.text_value() == text;
}

bool is_number_snapshot(
    const fastxlsx::WorksheetCellSnapshot& cell,
    std::uint32_t row,
    std::uint32_t column,
    double value)
{
    return cell.reference.row == row &&
        cell.reference.column == column &&
        cell.value.kind() == fastxlsx::CellValueKind::Number &&
        cell.value.number_value() == value;
}

bool is_blank_snapshot(
    const fastxlsx::WorksheetCellSnapshot& cell,
    std::uint32_t row,
    std::uint32_t column)
{
    return cell.reference.row == row &&
        cell.reference.column == column &&
        cell.value.kind() == fastxlsx::CellValueKind::Blank;
}

bool is_used_range(
    const std::optional<fastxlsx::CellRange>& range,
    std::uint32_t first_row,
    std::uint32_t first_column,
    std::uint32_t last_row,
    std::uint32_t last_column)
{
    return range.has_value() &&
        range->first_row == first_row &&
        range->first_column == first_column &&
        range->last_row == last_row &&
        range->last_column == last_column;
}

template <typename Callable>
bool threw_fastxlsx_error(Callable&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

std::filesystem::path artifact(std::string_view file_name)
{
    return fastxlsx::test::artifact_path(file_name);
}

std::filesystem::path write_generated_source_workbook()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-snapshot-source.xlsx");

    fastxlsx::Workbook workbook = fastxlsx::Workbook::create();
    fastxlsx::Worksheet& data = workbook.add_worksheet("Data");
    data.append_row({
        fastxlsx::Cell::text("alpha"),
        fastxlsx::Cell::number(2.0),
    });
    data.append_row({fastxlsx::Cell::text("tail")});

    fastxlsx::Worksheet& audit = workbook.add_worksheet("Audit");
    audit.append_row({fastxlsx::Cell::text("untouched")});

    workbook.save(source);
    return source;
}

void check_initial_sparse_snapshots(fastxlsx::WorksheetEditor& sheet)
{
    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        sheet.sparse_cells();
    check(all_cells.size() == 3 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_number_snapshot(all_cells[1], 1, 2, 2.0) &&
            is_text_snapshot(all_cells[2], 2, 1, "tail"),
        "generated source sparse_cells should expose row-major source values");

    const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
        sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    check(range_cells.size() == 3 &&
            is_text_snapshot(range_cells[0], 1, 1, "alpha") &&
            is_number_snapshot(range_cells[1], 1, 2, 2.0) &&
            is_text_snapshot(range_cells[2], 2, 1, "tail"),
        "generated source sparse_cells(CellRange) should expose row-major source values");

    const std::vector<fastxlsx::WorksheetCellSnapshot> a1_range_cells =
        sheet.sparse_cells("A1:B2");
    check(a1_range_cells.size() == 3 &&
            is_text_snapshot(a1_range_cells[0], 1, 1, "alpha") &&
            is_number_snapshot(a1_range_cells[1], 1, 2, 2.0) &&
            is_text_snapshot(a1_range_cells[2], 2, 1, "tail"),
        "generated source sparse_cells(A1 range) should expose row-major source values");

    const std::array<fastxlsx::WorksheetCellReference, 4> requested_cells {{
        {2, 1},
        {1, 2},
        {3, 3},
        {2, 1},
    }};
    const std::vector<fastxlsx::WorksheetCellSnapshot> batch_cells =
        sheet.sparse_cells(std::span<const fastxlsx::WorksheetCellReference>(
            requested_cells.data(), requested_cells.size()));
    check(batch_cells.size() == 3 &&
            is_text_snapshot(batch_cells[0], 2, 1, "tail") &&
            is_number_snapshot(batch_cells[1], 1, 2, 2.0) &&
            is_text_snapshot(batch_cells[2], 2, 1, "tail"),
        "generated source sparse_cells(batch) should keep requested order, duplicates, and missing-cell skips");
}

void check_initial_snapshots(fastxlsx::WorksheetEditor& sheet)
{
    check(sheet.cell_count() == 3,
        "generated source snapshot should materialize three cells");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 2),
        "generated source snapshot should expose the sparse used range");
    check_initial_sparse_snapshots(sheet);

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == "alpha" &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 2.0,
        "generated source row_cells should expose row-major source values");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        sheet.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[0].value.text_value() == "alpha" &&
            column_one[1].reference.row == 2 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "tail",
        "generated source column_cells should expose column-major source values");
}

void check_reopened_sparse_snapshots(fastxlsx::WorksheetEditor& data)
{
    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 4 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_number_snapshot(all_cells[1], 1, 2, 2.0) &&
            is_text_snapshot(all_cells[2], 2, 1, "tail") &&
            is_text_snapshot(all_cells[3], 2, 3, "snapshot-edit"),
        "reopened Data sparse_cells should expose row-major saved values");

    const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
        data.sparse_cells(fastxlsx::CellRange {1, 1, 2, 3});
    check(range_cells.size() == 4 &&
            is_text_snapshot(range_cells[0], 1, 1, "alpha") &&
            is_number_snapshot(range_cells[1], 1, 2, 2.0) &&
            is_text_snapshot(range_cells[2], 2, 1, "tail") &&
            is_text_snapshot(range_cells[3], 2, 3, "snapshot-edit"),
        "reopened Data sparse_cells(CellRange) should expose saved sparse values");

    const std::vector<fastxlsx::WorksheetCellSnapshot> a1_range_cells =
        data.sparse_cells("A1:C2");
    check(a1_range_cells.size() == 4 &&
            is_text_snapshot(a1_range_cells[0], 1, 1, "alpha") &&
            is_number_snapshot(a1_range_cells[1], 1, 2, 2.0) &&
            is_text_snapshot(a1_range_cells[2], 2, 1, "tail") &&
            is_text_snapshot(a1_range_cells[3], 2, 3, "snapshot-edit"),
        "reopened Data sparse_cells(A1 range) should expose saved sparse values");

    const std::array<fastxlsx::WorksheetCellReference, 4> requested_cells {{
        {2, 3},
        {1, 1},
        {3, 2},
        {2, 3},
    }};
    const std::vector<fastxlsx::WorksheetCellSnapshot> batch_cells =
        data.sparse_cells(std::span<const fastxlsx::WorksheetCellReference>(
            requested_cells.data(), requested_cells.size()));
    check(batch_cells.size() == 3 &&
            is_text_snapshot(batch_cells[0], 2, 3, "snapshot-edit") &&
            is_text_snapshot(batch_cells[1], 1, 1, "alpha") &&
            is_text_snapshot(batch_cells[2], 2, 3, "snapshot-edit"),
        "reopened Data sparse_cells(batch) should expose saved values in requested order");
}

void check_reopened_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened snapshot output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened snapshot output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened Data snapshot output should keep the sheet clean");
    check(data.cell_count() == 4,
        "reopened Data snapshot output should materialize the edited sparse cells");
    check(is_used_range(data.used_range(), 1, 1, 2, 3),
        "reopened Data snapshot output should expose the edited sparse used range");
    check(data.get_cell("C2").text_value() == "snapshot-edit",
        "reopened Data snapshot output should read the saved edit");
    check_reopened_sparse_snapshots(data);

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 2 &&
            row_two[0].reference.row == 2 &&
            row_two[0].reference.column == 1 &&
            row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_two[0].value.text_value() == "tail" &&
            row_two[1].reference.row == 2 &&
            row_two[1].reference.column == 3 &&
            row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
            row_two[1].value.text_value() == "snapshot-edit",
        "reopened Data row_cells should expose the saved sparse edit");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        data.column_cells(3);
    check(column_three.size() == 1 &&
            column_three[0].reference.row == 2 &&
            column_three[0].reference.column == 3 &&
            column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            column_three[0].value.text_value() == "snapshot-edit",
        "reopened Data column_cells should expose the saved sparse edit");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened Audit sheet should remain copy-original");
}

void check_erased_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened erased output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened erased output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened erased Data output should keep the sheet clean");
    check(data.cell_count() == 1,
        "reopened erased Data output should materialize only the remaining cell");
    check(is_used_range(data.used_range(), 1, 1, 1, 1),
        "reopened erased Data output should shrink the sparse used range");
    check(data.get_cell("A1").text_value() == "alpha",
        "reopened erased Data output should keep A1");
    check(!data.try_cell("B1").has_value(),
        "reopened erased Data output should omit erased B1");
    check(!data.try_cell("A2").has_value(),
        "reopened erased Data output should omit erased A2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 1 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha"),
        "reopened erased Data sparse_cells should expose only A1");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    check(row_one.size() == 1 &&
            is_text_snapshot(row_one[0], 1, 1, "alpha"),
        "reopened erased Data row_cells should expose only A1");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        data.column_cells(1);
    check(column_one.size() == 1 &&
            is_text_snapshot(column_one[0], 1, 1, "alpha"),
        "reopened erased Data column_cells should expose only A1");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened erased Audit sheet should remain copy-original");
}

void check_cleared_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened cleared output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened cleared output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened cleared Data output should keep the sheet clean");
    check(data.cell_count() == 3,
        "reopened cleared Data output should keep blank represented cells");
    check(is_used_range(data.used_range(), 1, 1, 2, 2),
        "reopened cleared Data output should keep the sparse used range");
    check(data.get_cell("A1").text_value() == "alpha",
        "reopened cleared Data output should keep A1");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened cleared Data output should keep B1 as a blank record");
    check(data.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened cleared Data output should keep A2 as a blank record");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 3 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_blank_snapshot(all_cells[2], 2, 1),
        "reopened cleared Data sparse_cells should expose text plus blanks");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    check(row_one.size() == 2 &&
            is_text_snapshot(row_one[0], 1, 1, "alpha") &&
            is_blank_snapshot(row_one[1], 1, 2),
        "reopened cleared Data row_cells should expose blank B1");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        data.column_cells(1);
    check(column_one.size() == 2 &&
            is_text_snapshot(column_one[0], 1, 1, "alpha") &&
            is_blank_snapshot(column_one[1], 2, 1),
        "reopened cleared Data column_cells should expose blank A2");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened cleared Audit sheet should remain copy-original");
}

void check_invalid_snapshot_reads_preserve_diagnostics(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::optional<std::string>& mutation_error)
{
    check(threw_fastxlsx_error([&sheet] { (void)sheet.row_cells(0); }),
        "row_cells should reject invalid row coordinates");
    check(editor.last_edit_error() == mutation_error,
        "row_cells invalid read should preserve prior last_edit_error");
    check(threw_fastxlsx_error([&sheet] { (void)sheet.column_cells(0); }),
        "column_cells should reject invalid column coordinates");
    check(editor.last_edit_error() == mutation_error,
        "column_cells invalid read should preserve prior last_edit_error");
    check(threw_fastxlsx_error([&sheet] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "sparse_cells(CellRange) should reject invalid ranges");
    check(editor.last_edit_error() == mutation_error,
        "sparse_cells(CellRange) invalid read should preserve prior last_edit_error");
    check(threw_fastxlsx_error([&sheet] { (void)sheet.sparse_cells("B2:A1"); }),
        "sparse_cells(A1 range) should reject reversed ranges");
    check(editor.last_edit_error() == mutation_error,
        "sparse_cells(A1 range) invalid read should preserve prior last_edit_error");

    const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {{
        {1, 1},
        {0, 1},
    }};
    check(threw_fastxlsx_error([&sheet, &invalid_batch] {
        (void)sheet.sparse_cells(std::span<const fastxlsx::WorksheetCellReference>(
            invalid_batch.data(), invalid_batch.size()));
    }), "sparse_cells(batch) should reject invalid coordinates");
    check(editor.last_edit_error() == mutation_error,
        "sparse_cells(batch) invalid read should preserve prior last_edit_error");
}

void test_generated_source_snapshot_edit_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(!editor.has_pending_changes(),
        "generated source snapshot editor should start clean");
    check(!sheet.has_pending_changes(),
        "generated source snapshot sheet should start clean");
    check_initial_snapshots(sheet);

    const bool invalid_mutation_failed = threw_fastxlsx_error([&sheet] {
        sheet.set_cell("a1",
            fastxlsx::CellValue::text("invalid-snapshot-payload"));
    });
    check(invalid_mutation_failed,
        "invalid mutation should seed last_edit_error before snapshot read failures");
    const std::optional<std::string> mutation_error = editor.last_edit_error();
    check(mutation_error.has_value(),
        "invalid mutation should expose last_edit_error");

    check_invalid_snapshot_reads_preserve_diagnostics(
        editor, sheet, mutation_error);
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "invalid snapshot reads should leave the session clean");
    check(sheet.cell_count() == 3,
        "invalid snapshot reads should preserve sparse cell count");
    check(!sheet.try_cell("C2").has_value(),
        "invalid snapshot reads should not create the later edit target");

    sheet.set_cell("C2", fastxlsx::CellValue::text("snapshot-edit"));
    check(!editor.last_edit_error().has_value(),
        "valid edit should clear prior snapshot diagnostics");
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "valid sparse edit should dirty the materialized session");
    check(editor.pending_materialized_cell_count() == 4,
        "valid sparse edit should expose dirty materialized cell count");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "snapshot save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "snapshot save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "snapshot-edit",
        "snapshot save_as should write the sparse edit");
    check_not_contains(data_xml, "invalid-snapshot-payload",
        "snapshot save_as should not leak the rejected payload");
    check_reopened_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean snapshot no-op save should keep output entries stable");
    check_reopened_output(noop_output);
}

void test_generated_source_erase_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-erase-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-erase-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.erase_cell("B1");
    sheet.erase_cell(2, 1);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "erase roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 1,
        "erase roundtrip should remove represented sparse records");
    check(editor.pending_materialized_cell_count() == 1,
        "erase roundtrip should expose the reduced dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 1, 1),
        "erase roundtrip should shrink the dirty sparse used range");
    check(!sheet.try_cell("B1").has_value(),
        "erase roundtrip should remove source-backed B1");
    check(!sheet.try_cell("A2").has_value(),
        "erase roundtrip should remove source-backed A2");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "erase roundtrip save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "erase roundtrip save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "erase roundtrip save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1\"",
        "erase roundtrip save_as should shrink the worksheet dimension");
    check_contains(data_xml, "alpha",
        "erase roundtrip save_as should keep the surviving source text");
    check_not_contains(data_xml, "tail",
        "erase roundtrip save_as should omit erased source text");
    check_not_contains(data_xml, "r=\"B1\"",
        "erase roundtrip save_as should omit erased B1");
    check_not_contains(data_xml, "r=\"A2\"",
        "erase roundtrip save_as should omit erased A2");
    check_erased_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean erased no-op save should keep output entries stable");
    check_erased_output(noop_output);
}

void test_generated_source_clear_value_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-clear-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-clear-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.clear_cell_value("B1");
    sheet.clear_cell_value(2, 1);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "clear roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 3,
        "clear roundtrip should keep represented sparse records");
    check(editor.pending_materialized_cell_count() == 3,
        "clear roundtrip should keep dirty materialized cell count stable");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 2),
        "clear roundtrip should keep the dirty sparse used range");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "clear roundtrip should convert source-backed B1 to blank");
    check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "clear roundtrip should convert source-backed A2 to blank");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "clear roundtrip save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "clear roundtrip save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clear roundtrip save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:B2\"",
        "clear roundtrip save_as should keep the worksheet dimension");
    check_contains(data_xml, "alpha",
        "clear roundtrip save_as should keep the surviving source text");
    check_contains(data_xml, "<c r=\"B1\"/>",
        "clear roundtrip save_as should project blank B1");
    check_contains(data_xml, "<c r=\"A2\"/>",
        "clear roundtrip save_as should project blank A2");
    check_not_contains(data_xml, "tail",
        "clear roundtrip save_as should omit cleared source text");
    check_not_contains(data_xml, "<v>2",
        "clear roundtrip save_as should omit cleared numeric value");
    check_cleared_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean cleared no-op save should keep output entries stable");
    check_cleared_output(noop_output);
}

} // namespace

int main()
{
    try {
        test_generated_source_snapshot_edit_roundtrip();
        test_generated_source_erase_roundtrip();
        test_generated_source_clear_value_roundtrip();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
