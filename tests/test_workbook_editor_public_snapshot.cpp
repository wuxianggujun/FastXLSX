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

bool is_boolean_snapshot(
    const fastxlsx::WorksheetCellSnapshot& cell,
    std::uint32_t row,
    std::uint32_t column,
    bool value)
{
    return cell.reference.row == row &&
        cell.reference.column == column &&
        cell.value.kind() == fastxlsx::CellValueKind::Boolean &&
        cell.value.boolean_value() == value;
}

bool is_formula_snapshot(
    const fastxlsx::WorksheetCellSnapshot& cell,
    std::uint32_t row,
    std::uint32_t column,
    std::string_view formula)
{
    return cell.reference.row == row &&
        cell.reference.column == column &&
        cell.value.kind() == fastxlsx::CellValueKind::Formula &&
        cell.value.text_value() == formula;
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

void check_all_erased_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened erase-all output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened erase-all output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened erase-all Data output should keep the sheet clean");
    check(data.cell_count() == 0,
        "reopened erase-all Data output should keep the sparse store empty");
    check(!data.used_range().has_value(),
        "reopened erase-all Data output should expose no sparse bounds");
    check(!data.try_cell("A1").has_value(),
        "reopened erase-all Data output should omit A1");
    check(!data.try_cell("B1").has_value(),
        "reopened erase-all Data output should omit B1");
    check(!data.try_cell("A2").has_value(),
        "reopened erase-all Data output should omit A2");
    check(!data.try_cell("C2").has_value(),
        "reopened erase-all Data output should omit C2");
    check(data.sparse_cells().empty(),
        "reopened erase-all Data sparse_cells should stay empty");
    check(data.row_cells(1).empty() && data.row_cells(2).empty(),
        "reopened erase-all Data row_cells should stay empty");
    check(data.column_cells(1).empty() && data.column_cells(2).empty() &&
            data.column_cells(3).empty(),
        "reopened erase-all Data column_cells should stay empty");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened erase-all Audit sheet should remain copy-original");
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

void check_all_cleared_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened clear-all output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened clear-all output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened clear-all Data output should keep the sheet clean");
    check(data.cell_count() == 4,
        "reopened clear-all Data output should keep every represented cell");
    check(is_used_range(data.used_range(), 1, 1, 2, 3),
        "reopened clear-all Data output should keep sparse bounds");
    check(data.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened clear-all Data output should keep A1 blank");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened clear-all Data output should keep B1 blank");
    check(data.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened clear-all Data output should keep A2 blank");
    check(data.get_cell("C2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened clear-all Data output should keep C2 blank");
    check(!data.try_cell("B2").has_value(),
        "reopened clear-all Data output should not synthesize B2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 4 &&
            is_blank_snapshot(all_cells[0], 1, 1) &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_blank_snapshot(all_cells[2], 2, 1) &&
            is_blank_snapshot(all_cells[3], 2, 3),
        "reopened clear-all Data sparse_cells should expose row-major blanks");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 2 &&
            is_blank_snapshot(row_two[0], 2, 1) &&
            is_blank_snapshot(row_two[1], 2, 3),
        "reopened clear-all Data row_cells should expose sparse row blanks");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        data.column_cells(3);
    check(column_three.size() == 1 &&
            is_blank_snapshot(column_three[0], 2, 3),
        "reopened clear-all Data column_cells should expose blank C2");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened clear-all Audit sheet should remain copy-original");
}

void check_row_column_cleared_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened row/column clear output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened row/column clear output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened row/column clear Data output should keep the sheet clean");
    check(data.cell_count() == 5,
        "reopened row/column clear Data output should keep represented blanks");
    check(is_used_range(data.used_range(), 1, 1, 2, 3),
        "reopened row/column clear Data output should preserve sparse bounds");
    check(data.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column clear Data output should keep A1 blank");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column clear Data output should keep B1 blank");
    check(data.get_cell("C1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column clear Data output should keep C1 blank");
    check(data.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column clear Data output should keep A2 blank");
    check(data.get_cell("C2").number_value() == 9.0,
        "reopened row/column clear Data output should retain non-target C2");
    check(!data.try_cell("B2").has_value(),
        "reopened row/column clear Data output should not synthesize B2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 5 &&
            is_blank_snapshot(all_cells[0], 1, 1) &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_blank_snapshot(all_cells[2], 1, 3) &&
            is_blank_snapshot(all_cells[3], 2, 1) &&
            is_number_snapshot(all_cells[4], 2, 3, 9.0),
        "reopened row/column clear Data sparse_cells should expose blanks plus C2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    check(row_one.size() == 3 &&
            is_blank_snapshot(row_one[0], 1, 1) &&
            is_blank_snapshot(row_one[1], 1, 2) &&
            is_blank_snapshot(row_one[2], 1, 3),
        "reopened row/column clear Data row_cells should expose blank row one");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 2 &&
            is_blank_snapshot(row_two[0], 2, 1) &&
            is_number_snapshot(row_two[1], 2, 3, 9.0),
        "reopened row/column clear Data row_cells should retain row-two C2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        data.column_cells(1);
    check(column_one.size() == 2 &&
            is_blank_snapshot(column_one[0], 1, 1) &&
            is_blank_snapshot(column_one[1], 2, 1),
        "reopened row/column clear Data column_cells should expose blank column one");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        data.column_cells(3);
    check(column_three.size() == 2 &&
            is_blank_snapshot(column_three[0], 1, 3) &&
            is_number_snapshot(column_three[1], 2, 3, 9.0),
        "reopened row/column clear Data column_cells should retain C2");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened row/column clear Audit sheet should remain copy-original");
}

void check_row_column_range_cleared_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened row/column range clear output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened row/column range clear output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened row/column range clear Data output should keep the sheet clean");
    check(data.cell_count() == 7,
        "reopened row/column range clear Data output should keep represented blanks");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened row/column range clear Data output should preserve sparse bounds");
    check(data.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column range clear Data output should keep A1 blank");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column range clear Data output should keep B1 blank");
    check(data.get_cell("C1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column range clear Data output should keep C1 blank");
    check(data.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column range clear Data output should keep A2 blank");
    check(data.get_cell("C2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column range clear Data output should keep C2 blank");
    check(data.get_cell("B3").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column range clear Data output should keep B3 blank");
    const fastxlsx::CellValue d3 = data.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "reopened row/column range clear Data output should retain D3");
    check(!data.try_cell("B2").has_value() &&
            !data.try_cell("C3").has_value(),
        "reopened row/column range clear Data output should not synthesize missing cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 7 &&
            is_blank_snapshot(all_cells[0], 1, 1) &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_blank_snapshot(all_cells[2], 1, 3) &&
            is_blank_snapshot(all_cells[3], 2, 1) &&
            is_blank_snapshot(all_cells[4], 2, 3) &&
            is_blank_snapshot(all_cells[5], 3, 2) &&
            is_boolean_snapshot(all_cells[6], 3, 4, true),
        "reopened row/column range clear Data sparse_cells should expose blanks plus D3");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
        data.row_cells(3);
    check(row_three.size() == 2 &&
            is_blank_snapshot(row_three[0], 3, 2) &&
            is_boolean_snapshot(row_three[1], 3, 4, true),
        "reopened row/column range clear Data row_cells should retain row-three D3");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
        data.column_cells(2);
    check(column_two.size() == 2 &&
            is_blank_snapshot(column_two[0], 1, 2) &&
            is_blank_snapshot(column_two[1], 3, 2),
        "reopened row/column range clear Data column_cells should expose blank column two");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
        data.column_cells(4);
    check(column_four.size() == 1 &&
            is_boolean_snapshot(column_four[0], 3, 4, true),
        "reopened row/column range clear Data column_cells should retain D3");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened row/column range clear Audit sheet should remain copy-original");
}

void check_row_column_erased_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened row/column erase output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened row/column erase output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened row/column erase Data output should keep the sheet clean");
    check(data.cell_count() == 1,
        "reopened row/column erase Data output should keep only C2");
    check(is_used_range(data.used_range(), 2, 3, 2, 3),
        "reopened row/column erase Data output should shrink to C2");
    check(data.get_cell("C2").number_value() == 9.0,
        "reopened row/column erase Data output should retain C2");
    check(!data.try_cell("A1").has_value(),
        "reopened row/column erase Data output should omit A1");
    check(!data.try_cell("B1").has_value(),
        "reopened row/column erase Data output should omit B1");
    check(!data.try_cell("C1").has_value(),
        "reopened row/column erase Data output should omit C1");
    check(!data.try_cell("A2").has_value(),
        "reopened row/column erase Data output should omit A2");
    check(!data.try_cell("B2").has_value(),
        "reopened row/column erase Data output should not synthesize B2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 1 &&
            is_number_snapshot(all_cells[0], 2, 3, 9.0),
        "reopened row/column erase Data sparse_cells should expose only C2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    check(row_one.empty(),
        "reopened row/column erase Data row_cells should omit row one");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 1 &&
            is_number_snapshot(row_two[0], 2, 3, 9.0),
        "reopened row/column erase Data row_cells should expose row-two C2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        data.column_cells(1);
    check(column_one.empty(),
        "reopened row/column erase Data column_cells should omit column one");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        data.column_cells(3);
    check(column_three.size() == 1 &&
            is_number_snapshot(column_three[0], 2, 3, 9.0),
        "reopened row/column erase Data column_cells should expose C2");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened row/column erase Audit sheet should remain copy-original");
}

void check_row_column_range_erased_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened row/column range erase output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened row/column range erase output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened row/column range erase Data output should keep the sheet clean");
    check(data.cell_count() == 1,
        "reopened row/column range erase Data output should keep only D3");
    check(is_used_range(data.used_range(), 3, 4, 3, 4),
        "reopened row/column range erase Data output should shrink to D3");
    const fastxlsx::CellValue d3 = data.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "reopened row/column range erase Data output should retain D3");
    check(!data.try_cell("A1").has_value(),
        "reopened row/column range erase Data output should omit A1");
    check(!data.try_cell("B1").has_value(),
        "reopened row/column range erase Data output should omit B1");
    check(!data.try_cell("C1").has_value(),
        "reopened row/column range erase Data output should omit C1");
    check(!data.try_cell("A2").has_value(),
        "reopened row/column range erase Data output should omit A2");
    check(!data.try_cell("C2").has_value(),
        "reopened row/column range erase Data output should omit C2");
    check(!data.try_cell("B3").has_value(),
        "reopened row/column range erase Data output should omit B3");
    check(!data.try_cell("B2").has_value() &&
            !data.try_cell("C3").has_value(),
        "reopened row/column range erase Data output should not synthesize missing cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 1 &&
            is_boolean_snapshot(all_cells[0], 3, 4, true),
        "reopened row/column range erase Data sparse_cells should expose only D3");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
        data.row_cells(3);
    check(row_three.size() == 1 &&
            is_boolean_snapshot(row_three[0], 3, 4, true),
        "reopened row/column range erase Data row_cells should expose D3");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
        data.column_cells(2);
    check(column_two.empty(),
        "reopened row/column range erase Data column_cells should omit column two");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
        data.column_cells(4);
    check(column_four.size() == 1 &&
            is_boolean_snapshot(column_four[0], 3, 4, true),
        "reopened row/column range erase Data column_cells should expose D3");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened row/column range erase Audit sheet should remain copy-original");
}

void check_appended_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened appended output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened appended output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened appended Data output should keep the sheet clean");
    check(data.cell_count() == 6,
        "reopened appended Data output should materialize source plus appended cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 3),
        "reopened appended Data output should expose the appended sparse used range");
    check(data.get_cell("A3").text_value() == "appended",
        "reopened appended Data output should read appended A3 text");
    check(data.get_cell("B3").number_value() == 4.0,
        "reopened appended Data output should read appended B3 number");
    const fastxlsx::CellValue reopened_c3 = data.get_cell("C3");
    check(reopened_c3.kind() == fastxlsx::CellValueKind::Boolean &&
            reopened_c3.boolean_value(),
        "reopened appended Data output should read appended C3 boolean");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 6 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_number_snapshot(all_cells[1], 1, 2, 2.0) &&
            is_text_snapshot(all_cells[2], 2, 1, "tail") &&
            is_text_snapshot(all_cells[3], 3, 1, "appended") &&
            is_number_snapshot(all_cells[4], 3, 2, 4.0) &&
            is_boolean_snapshot(all_cells[5], 3, 3, true),
        "reopened appended Data sparse_cells should expose source plus appended row");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
        data.row_cells(3);
    check(row_three.size() == 3 &&
            is_text_snapshot(row_three[0], 3, 1, "appended") &&
            is_number_snapshot(row_three[1], 3, 2, 4.0) &&
            is_boolean_snapshot(row_three[2], 3, 3, true),
        "reopened appended Data row_cells should expose the appended row");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        data.column_cells(3);
    check(column_three.size() == 1 &&
            is_boolean_snapshot(column_three[0], 3, 3, true),
        "reopened appended Data column_cells should expose appended C3");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened appended Audit sheet should remain copy-original");
}

void check_batch_replaced_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened batch output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened batch output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened batch Data output should keep the sheet clean");
    check(data.cell_count() == 5,
        "reopened batch Data output should materialize final sparse cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened batch Data output should expose final sparse bounds");
    check(data.get_cell("A1").text_value() == "batch-a",
        "reopened batch Data output should read overwritten A1");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened batch Data output should keep explicit B1 blank");
    check(data.get_cell("A2").text_value() == "tail",
        "reopened batch Data output should keep non-target source A2");
    const fastxlsx::CellValue c2 = data.get_cell("C2");
    check(c2.kind() == fastxlsx::CellValueKind::Formula &&
            c2.text_value() == "A1+B1",
        "reopened batch Data output should read later-wins C2 formula");
    const fastxlsx::CellValue d3 = data.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            !d3.boolean_value(),
        "reopened batch Data output should read inserted D3 boolean false");
    check(!data.try_cell("C1").has_value() &&
            !data.try_cell("B2").has_value(),
        "reopened batch Data output should not synthesize missing cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 5 &&
            is_text_snapshot(all_cells[0], 1, 1, "batch-a") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_text_snapshot(all_cells[2], 2, 1, "tail") &&
            is_formula_snapshot(all_cells[3], 2, 3, "A1+B1") &&
            is_boolean_snapshot(all_cells[4], 3, 4, false),
        "reopened batch Data sparse_cells should expose final row-major cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 2 &&
            is_text_snapshot(row_two[0], 2, 1, "tail") &&
            is_formula_snapshot(row_two[1], 2, 3, "A1+B1"),
        "reopened batch Data row_cells should expose sparse row two");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
        data.column_cells(4);
    check(column_four.size() == 1 &&
            is_boolean_snapshot(column_four[0], 3, 4, false),
        "reopened batch Data column_cells should expose inserted D3");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened batch Audit sheet should remain copy-original");
}

void check_value_batch_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened value-batch output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened value-batch output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened value-batch Data output should keep the sheet clean");
    check(data.cell_count() == 6,
        "reopened value-batch Data output should materialize final sparse cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened value-batch Data output should expose final sparse bounds");
    check(data.get_cell("A1").text_value() == "value-batch-a",
        "reopened value-batch Data output should read updated A1");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened value-batch Data output should keep explicit B1 blank");
    check(data.get_cell("C1").number_value() == 5.0,
        "reopened value-batch Data output should preserve non-target dirty C1");
    check(data.get_cell("A2").text_value() == "tail",
        "reopened value-batch Data output should keep non-target source A2");
    const fastxlsx::CellValue c2 = data.get_cell("C2");
    check(c2.kind() == fastxlsx::CellValueKind::Formula &&
            c2.text_value() == "A1+B1",
        "reopened value-batch Data output should read later-wins C2 formula");
    const fastxlsx::CellValue d3 = data.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "reopened value-batch Data output should read inserted D3 boolean true");
    check(!data.try_cell("B2").has_value() &&
            !data.try_cell("C3").has_value(),
        "reopened value-batch Data output should not synthesize missing cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 6 &&
            is_text_snapshot(all_cells[0], 1, 1, "value-batch-a") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_number_snapshot(all_cells[2], 1, 3, 5.0) &&
            is_text_snapshot(all_cells[3], 2, 1, "tail") &&
            is_formula_snapshot(all_cells[4], 2, 3, "A1+B1") &&
            is_boolean_snapshot(all_cells[5], 3, 4, true),
        "reopened value-batch Data sparse_cells should expose final row-major cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    check(row_one.size() == 3 &&
            is_text_snapshot(row_one[0], 1, 1, "value-batch-a") &&
            is_blank_snapshot(row_one[1], 1, 2) &&
            is_number_snapshot(row_one[2], 1, 3, 5.0),
        "reopened value-batch Data row_cells should expose row one");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        data.column_cells(3);
    check(column_three.size() == 2 &&
            is_number_snapshot(column_three[0], 1, 3, 5.0) &&
            is_formula_snapshot(column_three[1], 2, 3, "A1+B1"),
        "reopened value-batch Data column_cells should expose column three");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened value-batch Audit sheet should remain copy-original");
}

void check_row_column_replaced_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened row/column output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened row/column output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened row/column Data output should keep the sheet clean");
    check(data.cell_count() == 5,
        "reopened row/column Data output should materialize final sparse cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 3),
        "reopened row/column Data output should expose final sparse bounds");
    check(data.get_cell("A1").text_value() == "col-a",
        "reopened row/column Data output should read replaced A1");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column Data output should keep explicit B1 blank");
    check(data.get_cell("C1").number_value() == 8.0,
        "reopened row/column Data output should read retained C1 number");
    check(data.get_cell("A2").text_value() == "col-b",
        "reopened row/column Data output should read replaced A2");
    const fastxlsx::CellValue a3 = data.get_cell("A3");
    check(a3.kind() == fastxlsx::CellValueKind::Boolean &&
            !a3.boolean_value(),
        "reopened row/column Data output should read inserted A3 boolean false");
    check(!data.try_cell("B2").has_value() &&
            !data.try_cell("C2").has_value(),
        "reopened row/column Data output should not synthesize missing row-two cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 5 &&
            is_text_snapshot(all_cells[0], 1, 1, "col-a") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_number_snapshot(all_cells[2], 1, 3, 8.0) &&
            is_text_snapshot(all_cells[3], 2, 1, "col-b") &&
            is_boolean_snapshot(all_cells[4], 3, 1, false),
        "reopened row/column Data sparse_cells should expose final sparse cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    check(row_one.size() == 3 &&
            is_text_snapshot(row_one[0], 1, 1, "col-a") &&
            is_blank_snapshot(row_one[1], 1, 2) &&
            is_number_snapshot(row_one[2], 1, 3, 8.0),
        "reopened row/column Data row_cells should expose replaced row one");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        data.column_cells(1);
    check(column_one.size() == 3 &&
            is_text_snapshot(column_one[0], 1, 1, "col-a") &&
            is_text_snapshot(column_one[1], 2, 1, "col-b") &&
            is_boolean_snapshot(column_one[2], 3, 1, false),
        "reopened row/column Data column_cells should expose replaced column one");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened row/column Audit sheet should remain copy-original");
}

void check_row_column_value_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened row/column value output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened row/column value output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened row/column value Data output should keep the sheet clean");
    check(data.cell_count() == 5,
        "reopened row/column value Data output should materialize final sparse cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 3),
        "reopened row/column value Data output should expose final sparse bounds");
    check(data.get_cell("A1").text_value() == "value-col-a",
        "reopened row/column value Data output should read value-updated A1");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened row/column value Data output should keep explicit B1 blank");
    check(data.get_cell("C1").number_value() == 7.0,
        "reopened row/column value Data output should retain C1 beyond row prefix");
    check(data.get_cell("A2").text_value() == "value-col-b",
        "reopened row/column value Data output should read value-updated A2");
    const fastxlsx::CellValue a3 = data.get_cell("A3");
    check(a3.kind() == fastxlsx::CellValueKind::Boolean &&
            !a3.boolean_value(),
        "reopened row/column value Data output should retain A3 beyond column prefix");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 5 &&
            is_text_snapshot(all_cells[0], 1, 1, "value-col-a") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_number_snapshot(all_cells[2], 1, 3, 7.0) &&
            is_text_snapshot(all_cells[3], 2, 1, "value-col-b") &&
            is_boolean_snapshot(all_cells[4], 3, 1, false),
        "reopened row/column value Data sparse_cells should expose final sparse cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    check(row_one.size() == 3 &&
            is_text_snapshot(row_one[0], 1, 1, "value-col-a") &&
            is_blank_snapshot(row_one[1], 1, 2) &&
            is_number_snapshot(row_one[2], 1, 3, 7.0),
        "reopened row/column value Data row_cells should expose row prefix results");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        data.column_cells(1);
    check(column_one.size() == 3 &&
            is_text_snapshot(column_one[0], 1, 1, "value-col-a") &&
            is_text_snapshot(column_one[1], 2, 1, "value-col-b") &&
            is_boolean_snapshot(column_one[2], 3, 1, false),
        "reopened row/column value Data column_cells should expose column prefix results");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened row/column value Audit sheet should remain copy-original");
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

void test_generated_source_erase_all_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-erase-all-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-erase-all-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C2", fastxlsx::CellValue::text("erase-all-extra"));
    check(sheet.cell_count() == 4,
        "erase-all setup should add one dirty sparse cell");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "erase-all setup should expand sparse bounds");

    sheet.erase_cells();
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "erase-all roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 0,
        "erase-all roundtrip should remove every represented sparse record");
    check(editor.pending_materialized_cell_count() == 0,
        "erase-all roundtrip should expose zero dirty materialized cells");
    check(!sheet.used_range().has_value(),
        "erase-all roundtrip should leave the sparse used range empty");
    check(!sheet.try_cell("A1").has_value(),
        "erase-all roundtrip should remove source-backed A1");
    check(!sheet.try_cell("B1").has_value(),
        "erase-all roundtrip should remove source-backed B1");
    check(!sheet.try_cell("A2").has_value(),
        "erase-all roundtrip should remove source-backed A2");
    check(!sheet.try_cell("C2").has_value(),
        "erase-all roundtrip should remove dirty C2");
    check(sheet.sparse_cells().empty(),
        "erase-all roundtrip sparse_cells should stay empty");
    check(sheet.row_cells(1).empty() && sheet.row_cells(2).empty(),
        "erase-all roundtrip row_cells should stay empty");
    check(sheet.column_cells(1).empty() && sheet.column_cells(2).empty() &&
            sheet.column_cells(3).empty(),
        "erase-all roundtrip column_cells should stay empty");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "erase-all roundtrip save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "erase-all roundtrip save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "erase-all roundtrip save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, R"(<dimension ref="A1"/>)",
        "erase-all roundtrip save_as should project an empty worksheet dimension");
    check_not_contains(data_xml, R"(<c r=")",
        "erase-all roundtrip save_as should not write cell records");
    check_not_contains(data_xml, "alpha",
        "erase-all roundtrip save_as should omit erased source A1 text");
    check_not_contains(data_xml, "tail",
        "erase-all roundtrip save_as should omit erased source A2 text");
    check_not_contains(data_xml, "<v>2",
        "erase-all roundtrip save_as should omit erased source B1 number");
    check_not_contains(data_xml, "erase-all-extra",
        "erase-all roundtrip save_as should omit erased dirty C2 text");
    check_all_erased_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean erase-all no-op save should keep output entries stable");
    check_all_erased_output(noop_output);
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

void test_generated_source_clear_all_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-clear-all-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-clear-all-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C2", fastxlsx::CellValue::boolean(true));
    check(sheet.cell_count() == 4,
        "clear-all setup should add one dirty sparse cell");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "clear-all setup should expand sparse bounds");

    sheet.clear_cell_values();
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "clear-all roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 4,
        "clear-all roundtrip should keep represented sparse records");
    check(editor.pending_materialized_cell_count() == 4,
        "clear-all roundtrip should expose every blank record in diagnostics");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "clear-all roundtrip should keep the sparse used range");
    check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "clear-all roundtrip should convert source-backed A1 to blank");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "clear-all roundtrip should convert source-backed B1 to blank");
    check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "clear-all roundtrip should convert source-backed A2 to blank");
    check(sheet.get_cell("C2").kind() == fastxlsx::CellValueKind::Blank,
        "clear-all roundtrip should convert dirty C2 to blank");
    check(!sheet.try_cell("B2").has_value(),
        "clear-all roundtrip should not synthesize missing B2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        sheet.sparse_cells();
    check(all_cells.size() == 4 &&
            is_blank_snapshot(all_cells[0], 1, 1) &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_blank_snapshot(all_cells[2], 2, 1) &&
            is_blank_snapshot(all_cells[3], 2, 3),
        "clear-all roundtrip sparse_cells should expose represented blanks");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "clear-all roundtrip save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "clear-all roundtrip save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "clear-all roundtrip save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:C2\"",
        "clear-all roundtrip save_as should keep the expanded worksheet dimension");
    check_contains(data_xml, R"(<c r="A1"/>)",
        "clear-all roundtrip save_as should project blank A1");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "clear-all roundtrip save_as should project blank B1");
    check_contains(data_xml, R"(<c r="A2"/>)",
        "clear-all roundtrip save_as should project blank A2");
    check_contains(data_xml, R"(<c r="C2"/>)",
        "clear-all roundtrip save_as should project blank C2");
    check_not_contains(data_xml, R"(r="B2")",
        "clear-all roundtrip save_as should not synthesize B2");
    check_not_contains(data_xml, "alpha",
        "clear-all roundtrip save_as should omit cleared source A1 text");
    check_not_contains(data_xml, "tail",
        "clear-all roundtrip save_as should omit cleared source A2 text");
    check_not_contains(data_xml, "<v>2",
        "clear-all roundtrip save_as should omit cleared source B1 number");
    check_not_contains(data_xml, R"(<c r="C2" t="b"><v>1</v></c>)",
        "clear-all roundtrip save_as should omit cleared dirty C2 boolean");
    check_all_cleared_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean clear-all no-op save should keep output entries stable");
    check_all_cleared_output(noop_output);
}

void test_generated_source_row_column_clear_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-clear-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-clear-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C1", fastxlsx::CellValue::text("clear-row-c1"));
    sheet.set_cell("C2", fastxlsx::CellValue::number(9.0));
    check(sheet.cell_count() == 5,
        "row/column clear setup should add C1 and C2 sparse cells");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "row/column clear setup should expand sparse bounds");

    sheet.clear_row(1);
    check(sheet.cell_count() == 5,
        "clear_row should keep represented sparse records");
    check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "clear_row should convert source-backed A1 to blank");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "clear_row should convert source-backed B1 to blank");
    check(sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Blank,
        "clear_row should convert dirty C1 to blank");
    check(sheet.get_cell("A2").text_value() == "tail",
        "clear_row should leave non-target source-backed A2 untouched");
    check(sheet.get_cell("C2").number_value() == 9.0,
        "clear_row should leave non-target dirty C2 untouched");

    sheet.clear_column(1);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "row/column clear roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "row/column clear roundtrip should keep represented sparse records");
    check(editor.pending_materialized_cell_count() == 5,
        "row/column clear roundtrip should expose final dirty materialized count");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "row/column clear roundtrip should keep final sparse bounds");
    check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "clear_column should preserve already blank A1");
    check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "clear_column should convert source-backed A2 to blank");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "clear_column should preserve non-target B1 blank");
    check(sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Blank,
        "clear_column should preserve non-target C1 blank");
    check(sheet.get_cell("C2").number_value() == 9.0,
        "clear_column should preserve non-target C2 number");
    check(!sheet.try_cell("B2").has_value(),
        "row/column clear should not synthesize missing B2");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "row/column clear save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "row/column clear save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column clear save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:C2\"",
        "row/column clear save_as should write final worksheet dimension");
    check_contains(data_xml, R"(<c r="A1"/>)",
        "row/column clear save_as should write blank A1");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "row/column clear save_as should write blank B1");
    check_contains(data_xml, R"(<c r="C1"/>)",
        "row/column clear save_as should write blank C1");
    check_contains(data_xml, R"(<c r="A2"/>)",
        "row/column clear save_as should write blank A2");
    check_contains(data_xml, R"(<c r="C2"><v>9</v></c>)",
        "row/column clear save_as should preserve non-target C2 number");
    check_not_contains(data_xml, "alpha",
        "row/column clear save_as should omit cleared source A1 text");
    check_not_contains(data_xml, "tail",
        "row/column clear save_as should omit cleared source A2 text");
    check_not_contains(data_xml, "clear-row-c1",
        "row/column clear save_as should omit cleared dirty C1 text");
    check_not_contains(data_xml, "<v>2",
        "row/column clear save_as should omit cleared source B1 number");
    check_row_column_cleared_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean row/column clear no-op save should keep output entries stable");
    check_row_column_cleared_output(noop_output);
}

void test_generated_source_row_column_range_clear_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-range-clear-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-range-clear-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C1", fastxlsx::CellValue::text("clear-range-c1"));
    sheet.set_cell("C2", fastxlsx::CellValue::number(9.0));
    sheet.set_cell("B3", fastxlsx::CellValue::text("clear-range-b3"));
    sheet.set_cell("D3", fastxlsx::CellValue::boolean(true));
    check(sheet.cell_count() == 7,
        "row/column range clear setup should add dirty sparse cells");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "row/column range clear setup should expand sparse bounds");

    sheet.clear_rows(1, 2);
    check(sheet.cell_count() == 7,
        "clear_rows should keep represented sparse records");
    check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "clear_rows should convert source-backed A1 to blank");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "clear_rows should convert source-backed B1 to blank");
    check(sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Blank,
        "clear_rows should convert dirty C1 to blank");
    check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "clear_rows should convert source-backed A2 to blank");
    check(sheet.get_cell("C2").kind() == fastxlsx::CellValueKind::Blank,
        "clear_rows should convert dirty C2 to blank");
    check(sheet.get_cell("B3").text_value() == "clear-range-b3",
        "clear_rows should leave non-target B3 untouched");
    check(sheet.get_cell("D3").boolean_value(),
        "clear_rows should leave non-target D3 untouched");

    sheet.clear_columns(2, 3);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "row/column range clear roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 7,
        "row/column range clear roundtrip should keep represented sparse records");
    check(editor.pending_materialized_cell_count() == 7,
        "row/column range clear roundtrip should expose final dirty materialized count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "row/column range clear roundtrip should keep final sparse bounds");
    check(sheet.get_cell("B3").kind() == fastxlsx::CellValueKind::Blank,
        "clear_columns should convert dirty B3 to blank");
    check(sheet.get_cell("D3").boolean_value(),
        "clear_columns should preserve non-target D3");
    check(!sheet.try_cell("B2").has_value() &&
            !sheet.try_cell("C3").has_value(),
        "row/column range clear should not synthesize missing cells");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "row/column range clear save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "row/column range clear save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column range clear save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "row/column range clear save_as should write final worksheet dimension");
    check_contains(data_xml, R"(<c r="A1"/>)",
        "row/column range clear save_as should write blank A1");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "row/column range clear save_as should write blank B1");
    check_contains(data_xml, R"(<c r="C1"/>)",
        "row/column range clear save_as should write blank C1");
    check_contains(data_xml, R"(<c r="A2"/>)",
        "row/column range clear save_as should write blank A2");
    check_contains(data_xml, R"(<c r="C2"/>)",
        "row/column range clear save_as should write blank C2");
    check_contains(data_xml, R"(<c r="B3"/>)",
        "row/column range clear save_as should write blank B3");
    check_contains(data_xml, R"(<c r="D3" t="b"><v>1</v></c>)",
        "row/column range clear save_as should preserve non-target D3 boolean");
    check_not_contains(data_xml, "alpha",
        "row/column range clear save_as should omit cleared source A1 text");
    check_not_contains(data_xml, "tail",
        "row/column range clear save_as should omit cleared source A2 text");
    check_not_contains(data_xml, "clear-range-c1",
        "row/column range clear save_as should omit cleared dirty C1 text");
    check_not_contains(data_xml, "clear-range-b3",
        "row/column range clear save_as should omit cleared dirty B3 text");
    check_not_contains(data_xml, "<v>2",
        "row/column range clear save_as should omit cleared source B1 number");
    check_not_contains(data_xml, "<v>9",
        "row/column range clear save_as should omit cleared C2 number");
    check_row_column_range_cleared_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean row/column range clear no-op save should keep output entries stable");
    check_row_column_range_cleared_output(noop_output);
}

void test_generated_source_row_column_erase_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-erase-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-erase-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C1", fastxlsx::CellValue::text("erase-row-c1"));
    sheet.set_cell("C2", fastxlsx::CellValue::number(9.0));
    check(sheet.cell_count() == 5,
        "row/column erase setup should add C1 and C2 sparse cells");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "row/column erase setup should expand sparse bounds");

    sheet.erase_row(1);
    check(sheet.cell_count() == 2,
        "erase_row should remove represented row-one sparse records");
    check(sheet.row_cells(1).empty(),
        "erase_row should remove row-one snapshots");
    check(sheet.get_cell("A2").text_value() == "tail",
        "erase_row should leave non-target source-backed A2 untouched");
    check(sheet.get_cell("C2").number_value() == 9.0,
        "erase_row should leave non-target dirty C2 untouched");
    check(!sheet.try_cell("A1").has_value() &&
            !sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("C1").has_value(),
        "erase_row should omit source-backed and dirty row-one cells");

    sheet.erase_column(1);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "row/column erase roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 1,
        "row/column erase roundtrip should remove represented sparse records");
    check(editor.pending_materialized_cell_count() == 1,
        "row/column erase roundtrip should expose final dirty materialized count");
    check(is_used_range(sheet.used_range(), 2, 3, 2, 3),
        "row/column erase roundtrip should shrink final sparse bounds");
    check(sheet.get_cell("C2").number_value() == 9.0,
        "erase_column should preserve non-target C2 number");
    check(sheet.column_cells(1).empty(),
        "erase_column should remove column-one snapshots");
    check(!sheet.try_cell("A2").has_value(),
        "erase_column should remove source-backed A2");
    check(!sheet.try_cell("B2").has_value(),
        "row/column erase should not synthesize missing B2");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "row/column erase save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "row/column erase save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column erase save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"C2\"",
        "row/column erase save_as should shrink the worksheet dimension to C2");
    check_contains(data_xml, R"(<c r="C2"><v>9</v></c>)",
        "row/column erase save_as should preserve non-target C2 number");
    check_not_contains(data_xml, "alpha",
        "row/column erase save_as should omit erased source A1 text");
    check_not_contains(data_xml, "tail",
        "row/column erase save_as should omit erased source A2 text");
    check_not_contains(data_xml, "erase-row-c1",
        "row/column erase save_as should omit erased dirty C1 text");
    check_not_contains(data_xml, "r=\"A1\"",
        "row/column erase save_as should omit A1");
    check_not_contains(data_xml, "r=\"B1\"",
        "row/column erase save_as should omit B1");
    check_not_contains(data_xml, "r=\"C1\"",
        "row/column erase save_as should omit C1");
    check_not_contains(data_xml, "r=\"A2\"",
        "row/column erase save_as should omit A2");
    check_row_column_erased_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean row/column erase no-op save should keep output entries stable");
    check_row_column_erased_output(noop_output);
}

void test_generated_source_row_column_range_erase_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-range-erase-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-range-erase-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C1", fastxlsx::CellValue::text("erase-range-c1"));
    sheet.set_cell("C2", fastxlsx::CellValue::number(9.0));
    sheet.set_cell("B3", fastxlsx::CellValue::text("erase-range-b3"));
    sheet.set_cell("D3", fastxlsx::CellValue::boolean(true));
    check(sheet.cell_count() == 7,
        "row/column range erase setup should add dirty sparse cells");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "row/column range erase setup should expand sparse bounds");

    sheet.erase_rows(1, 2);
    check(sheet.cell_count() == 2,
        "erase_rows should remove represented sparse records in rows one and two");
    check(sheet.row_cells(1).empty() &&
            sheet.row_cells(2).empty(),
        "erase_rows should remove row-one and row-two snapshots");
    check(sheet.get_cell("B3").text_value() == "erase-range-b3",
        "erase_rows should leave non-target B3 untouched");
    check(sheet.get_cell("D3").boolean_value(),
        "erase_rows should leave non-target D3 untouched");
    check(!sheet.try_cell("A1").has_value() &&
            !sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("C1").has_value() &&
            !sheet.try_cell("A2").has_value() &&
            !sheet.try_cell("C2").has_value(),
        "erase_rows should omit source-backed and dirty row-range cells");

    sheet.erase_columns(2, 3);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "row/column range erase roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 1,
        "row/column range erase roundtrip should remove represented sparse records");
    check(editor.pending_materialized_cell_count() == 1,
        "row/column range erase roundtrip should expose final dirty materialized count");
    check(is_used_range(sheet.used_range(), 3, 4, 3, 4),
        "row/column range erase roundtrip should shrink final sparse bounds");
    check(sheet.get_cell("D3").boolean_value(),
        "erase_columns should preserve non-target D3");
    check(sheet.column_cells(2).empty() &&
            sheet.column_cells(3).empty(),
        "erase_columns should remove target-column snapshots");
    check(!sheet.try_cell("B3").has_value(),
        "erase_columns should remove dirty B3");
    check(!sheet.try_cell("B2").has_value() &&
            !sheet.try_cell("C3").has_value(),
        "row/column range erase should not synthesize missing cells");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "row/column range erase save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "row/column range erase save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column range erase save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"D3\"",
        "row/column range erase save_as should shrink the worksheet dimension to D3");
    check_contains(data_xml, R"(<c r="D3" t="b"><v>1</v></c>)",
        "row/column range erase save_as should preserve non-target D3 boolean");
    check_not_contains(data_xml, "alpha",
        "row/column range erase save_as should omit erased source A1 text");
    check_not_contains(data_xml, "tail",
        "row/column range erase save_as should omit erased source A2 text");
    check_not_contains(data_xml, "erase-range-c1",
        "row/column range erase save_as should omit erased dirty C1 text");
    check_not_contains(data_xml, "erase-range-b3",
        "row/column range erase save_as should omit erased dirty B3 text");
    check_not_contains(data_xml, "r=\"A1\"",
        "row/column range erase save_as should omit A1");
    check_not_contains(data_xml, "r=\"B1\"",
        "row/column range erase save_as should omit B1");
    check_not_contains(data_xml, "r=\"C1\"",
        "row/column range erase save_as should omit C1");
    check_not_contains(data_xml, "r=\"A2\"",
        "row/column range erase save_as should omit A2");
    check_not_contains(data_xml, "r=\"C2\"",
        "row/column range erase save_as should omit C2");
    check_not_contains(data_xml, "r=\"B3\"",
        "row/column range erase save_as should omit B3");
    check_row_column_range_erased_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean row/column range erase no-op save should keep output entries stable");
    check_row_column_range_erased_output(noop_output);
}

void test_generated_source_append_row_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-append-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-append-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.append_row({
        fastxlsx::CellValue::text("appended"),
        fastxlsx::CellValue::number(4.0),
        fastxlsx::CellValue::boolean(true),
    });
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "append-row roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 6,
        "append-row roundtrip should add three represented sparse records");
    check(editor.pending_materialized_cell_count() == 6,
        "append-row roundtrip should expose source plus appended dirty materialized cells");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 3),
        "append-row roundtrip should expand the dirty sparse used range");
    check(sheet.get_cell("A3").text_value() == "appended",
        "append-row roundtrip should expose appended A3 text");
    check(sheet.get_cell("B3").number_value() == 4.0,
        "append-row roundtrip should expose appended B3 number");
    const fastxlsx::CellValue appended_c3 = sheet.get_cell("C3");
    check(appended_c3.kind() == fastxlsx::CellValueKind::Boolean &&
            appended_c3.boolean_value(),
        "append-row roundtrip should expose appended C3 boolean");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
        sheet.row_cells(3);
    check(row_three.size() == 3 &&
            is_text_snapshot(row_three[0], 3, 1, "appended") &&
            is_number_snapshot(row_three[1], 3, 2, 4.0) &&
            is_boolean_snapshot(row_three[2], 3, 3, true),
        "append-row roundtrip should expose the appended row snapshot");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "append-row roundtrip save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "append-row roundtrip save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "append-row roundtrip save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:C3\"",
        "append-row roundtrip save_as should expand the worksheet dimension");
    check_contains(data_xml, "appended",
        "append-row roundtrip save_as should write appended text");
    check_contains(data_xml, R"(<c r="B3"><v>4</v></c>)",
        "append-row roundtrip save_as should write appended number");
    check_contains(data_xml, R"(<c r="C3" t="b"><v>1</v></c>)",
        "append-row roundtrip save_as should write appended boolean");
    check_appended_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean appended no-op save should keep output entries stable");
    check_appended_output(noop_output);
}

void test_generated_source_sparse_batch_replacement_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-batch-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-batch-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cells({
        {fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::CellValue::text("batch-a")},
        {fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::CellValue::blank()},
        {fastxlsx::WorksheetCellReference {2, 3},
            fastxlsx::CellValue::text("batch-first-c2")},
        {fastxlsx::WorksheetCellReference {2, 3},
            fastxlsx::CellValue::formula("A1+B1")},
        {fastxlsx::WorksheetCellReference {3, 4},
            fastxlsx::CellValue::boolean(false)},
    });
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "sparse batch replacement should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "sparse batch replacement should expose final represented sparse cells");
    check(editor.pending_materialized_cell_count() == 5,
        "sparse batch replacement should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "sparse batch replacement should expose final sparse bounds");
    check(sheet.get_cell("A1").text_value() == "batch-a",
        "sparse batch replacement should overwrite source A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "sparse batch replacement should convert source B1 to blank");
    check(sheet.get_cell("A2").text_value() == "tail",
        "sparse batch replacement should preserve non-target source A2");
    const fastxlsx::CellValue c2 = sheet.get_cell("C2");
    check(c2.kind() == fastxlsx::CellValueKind::Formula &&
            c2.text_value() == "A1+B1",
        "sparse batch replacement should use the later duplicate C2 update");
    const fastxlsx::CellValue d3 = sheet.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            !d3.boolean_value(),
        "sparse batch replacement should insert D3 boolean false");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "sparse batch replacement save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "sparse batch replacement save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "sparse batch replacement save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "sparse batch replacement save_as should write final worksheet dimension");
    check_contains(data_xml, "batch-a",
        "sparse batch replacement save_as should write replaced A1 text");
    check_contains(data_xml, "tail",
        "sparse batch replacement save_as should keep non-target A2 text");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "sparse batch replacement save_as should write explicit B1 blank");
    check_contains(data_xml, R"(<c r="C2"><f>A1+B1</f></c>)",
        "sparse batch replacement save_as should write later-wins C2 formula");
    check_contains(data_xml, R"(<c r="D3" t="b"><v>0</v></c>)",
        "sparse batch replacement save_as should write inserted D3 boolean false");
    check_not_contains(data_xml, "alpha",
        "sparse batch replacement save_as should omit overwritten source A1 text");
    check_not_contains(data_xml, "<v>2",
        "sparse batch replacement save_as should omit overwritten source B1 number");
    check_not_contains(data_xml, "batch-first-c2",
        "sparse batch replacement save_as should omit overwritten duplicate C2 text");
    check_batch_replaced_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean sparse batch replacement no-op save should keep output entries stable");
    check_batch_replaced_output(noop_output);
}

void test_generated_source_sparse_value_batch_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-value-batch-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-value-batch-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C1", fastxlsx::CellValue::number(5.0));
    check(sheet.cell_count() == 4,
        "value-batch setup should add one non-target dirty cell");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "value-batch setup should expand sparse bounds");

    sheet.set_cell_values({
        {fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::CellValue::text("value-batch-a")},
        {fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::CellValue::blank()},
        {fastxlsx::WorksheetCellReference {2, 3},
            fastxlsx::CellValue::text("value-batch-first-c2")},
        {fastxlsx::WorksheetCellReference {2, 3},
            fastxlsx::CellValue::formula("A1+B1")},
        {fastxlsx::WorksheetCellReference {3, 4},
            fastxlsx::CellValue::boolean(true)},
    });
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "sparse value-batch should dirty the materialized session");
    check(sheet.cell_count() == 6,
        "sparse value-batch should expose final represented sparse cells");
    check(editor.pending_materialized_cell_count() == 6,
        "sparse value-batch should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "sparse value-batch should expose final sparse bounds");
    check(sheet.get_cell("A1").text_value() == "value-batch-a",
        "sparse value-batch should update source A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "sparse value-batch should convert source B1 to blank");
    check(sheet.get_cell("C1").number_value() == 5.0,
        "sparse value-batch should preserve non-target dirty C1");
    check(sheet.get_cell("A2").text_value() == "tail",
        "sparse value-batch should preserve non-target source A2");
    const fastxlsx::CellValue c2 = sheet.get_cell("C2");
    check(c2.kind() == fastxlsx::CellValueKind::Formula &&
            c2.text_value() == "A1+B1",
        "sparse value-batch should use the later duplicate C2 update");
    const fastxlsx::CellValue d3 = sheet.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "sparse value-batch should insert D3 boolean true");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "sparse value-batch save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "sparse value-batch save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "sparse value-batch save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "sparse value-batch save_as should write final worksheet dimension");
    check_contains(data_xml, "value-batch-a",
        "sparse value-batch save_as should write updated A1 text");
    check_contains(data_xml, "tail",
        "sparse value-batch save_as should keep non-target A2 text");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "sparse value-batch save_as should write explicit B1 blank");
    check_contains(data_xml, R"(<c r="C1"><v>5</v></c>)",
        "sparse value-batch save_as should preserve non-target dirty C1");
    check_contains(data_xml, R"(<c r="C2"><f>A1+B1</f></c>)",
        "sparse value-batch save_as should write later-wins C2 formula");
    check_contains(data_xml, R"(<c r="D3" t="b"><v>1</v></c>)",
        "sparse value-batch save_as should write inserted D3 boolean true");
    check_not_contains(data_xml, "alpha",
        "sparse value-batch save_as should omit overwritten source A1 text");
    check_not_contains(data_xml, "<v>2",
        "sparse value-batch save_as should omit overwritten source B1 number");
    check_not_contains(data_xml, "value-batch-first-c2",
        "sparse value-batch save_as should omit overwritten duplicate C2 text");
    check_value_batch_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean sparse value-batch no-op save should keep output entries stable");
    check_value_batch_output(noop_output);
}

void test_generated_source_row_column_replacement_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_row(1, {
        fastxlsx::CellValue::text("row-a"),
        fastxlsx::CellValue::blank(),
        fastxlsx::CellValue::number(8.0),
    });
    check(sheet.cell_count() == 4,
        "row replacement should replace row one and keep source-backed row two");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "row replacement should expand row-one sparse bounds");
    check(sheet.get_cell("A1").text_value() == "row-a",
        "row replacement should expose replacement A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "row replacement should expose explicit B1 blank");
    check(sheet.get_cell("C1").number_value() == 8.0,
        "row replacement should expose replacement C1 number");
    check(sheet.get_cell("A2").text_value() == "tail",
        "row replacement should keep non-target source-backed A2");

    sheet.set_column(1, {
        fastxlsx::CellValue::text("col-a"),
        fastxlsx::CellValue::text("col-b"),
        fastxlsx::CellValue::boolean(false),
    });
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "row/column replacement should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "row/column replacement should expose final represented sparse records");
    check(editor.pending_materialized_cell_count() == 5,
        "row/column replacement should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 3),
        "row/column replacement should expose final sparse bounds");
    check(sheet.get_cell("A1").text_value() == "col-a",
        "column replacement should overwrite row replacement A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "column replacement should preserve non-target explicit B1 blank");
    check(sheet.get_cell("C1").number_value() == 8.0,
        "column replacement should preserve non-target C1 number");
    check(sheet.get_cell("A2").text_value() == "col-b",
        "column replacement should overwrite source-backed A2");
    const fastxlsx::CellValue a3 = sheet.get_cell("A3");
    check(a3.kind() == fastxlsx::CellValueKind::Boolean &&
            !a3.boolean_value(),
        "column replacement should insert A3 boolean false");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "row/column replacement save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "row/column replacement save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column replacement save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:C3\"",
        "row/column replacement save_as should write final worksheet dimension");
    check_contains(data_xml, "col-a",
        "row/column replacement save_as should write replaced A1 text");
    check_contains(data_xml, "col-b",
        "row/column replacement save_as should write replaced A2 text");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "row/column replacement save_as should write explicit B1 blank");
    check_contains(data_xml, R"(<c r="C1"><v>8</v></c>)",
        "row/column replacement save_as should write retained C1 number");
    check_contains(data_xml, R"(<c r="A3" t="b"><v>0</v></c>)",
        "row/column replacement save_as should write inserted A3 boolean false");
    check_not_contains(data_xml, "alpha",
        "row/column replacement save_as should omit overwritten source A1 text");
    check_not_contains(data_xml, "tail",
        "row/column replacement save_as should omit overwritten source A2 text");
    check_not_contains(data_xml, "row-a",
        "row/column replacement save_as should omit overwritten intermediate A1 text");
    check_row_column_replaced_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean row/column no-op save should keep output entries stable");
    check_row_column_replaced_output(noop_output);
}

void test_generated_source_row_column_value_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-value-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-row-column-value-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C1", fastxlsx::CellValue::number(7.0));
    sheet.set_cell("A3", fastxlsx::CellValue::boolean(false));
    check(sheet.cell_count() == 5,
        "row/column value roundtrip setup should add C1 and A3 sparse cells");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 3),
        "row/column value roundtrip setup should expand sparse bounds");

    sheet.set_row_values(1, {
        fastxlsx::CellValue::text("value-row-a"),
        fastxlsx::CellValue::blank(),
    });
    check(sheet.cell_count() == 5,
        "row value prefix should keep represented sparse cell count stable");
    check(sheet.get_cell("A1").text_value() == "value-row-a",
        "row value prefix should update A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "row value prefix should convert B1 to explicit blank");
    check(sheet.get_cell("C1").number_value() == 7.0,
        "row value prefix should leave C1 beyond the prefix untouched");
    check(sheet.get_cell("A2").text_value() == "tail",
        "row value prefix should leave non-target source-backed A2 untouched");

    sheet.set_column_values(1, {
        fastxlsx::CellValue::text("value-col-a"),
        fastxlsx::CellValue::text("value-col-b"),
    });
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "row/column value roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "row/column value roundtrip should keep final represented sparse count");
    check(editor.pending_materialized_cell_count() == 5,
        "row/column value roundtrip should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 3),
        "row/column value roundtrip should keep final sparse bounds");
    check(sheet.get_cell("A1").text_value() == "value-col-a",
        "column value prefix should overwrite A1 value");
    check(sheet.get_cell("A2").text_value() == "value-col-b",
        "column value prefix should overwrite A2 value");
    const fastxlsx::CellValue a3 = sheet.get_cell("A3");
    check(a3.kind() == fastxlsx::CellValueKind::Boolean &&
            !a3.boolean_value(),
        "column value prefix should leave A3 beyond the prefix untouched");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "column value prefix should preserve non-target explicit B1 blank");
    check(sheet.get_cell("C1").number_value() == 7.0,
        "column value prefix should preserve non-target C1 number");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "row/column value save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "row/column value save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column value save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:C3\"",
        "row/column value save_as should write final worksheet dimension");
    check_contains(data_xml, "value-col-a",
        "row/column value save_as should write final A1 text");
    check_contains(data_xml, "value-col-b",
        "row/column value save_as should write final A2 text");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "row/column value save_as should write explicit B1 blank");
    check_contains(data_xml, R"(<c r="C1"><v>7</v></c>)",
        "row/column value save_as should preserve C1 beyond the row prefix");
    check_contains(data_xml, R"(<c r="A3" t="b"><v>0</v></c>)",
        "row/column value save_as should preserve A3 beyond the column prefix");
    check_not_contains(data_xml, "alpha",
        "row/column value save_as should omit overwritten source A1 text");
    check_not_contains(data_xml, "tail",
        "row/column value save_as should omit overwritten source A2 text");
    check_not_contains(data_xml, "value-row-a",
        "row/column value save_as should omit overwritten intermediate A1 text");
    check_row_column_value_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean row/column value no-op save should keep output entries stable");
    check_row_column_value_output(noop_output);
}

} // namespace

int main()
{
    try {
        test_generated_source_snapshot_edit_roundtrip();
        test_generated_source_erase_roundtrip();
        test_generated_source_erase_all_roundtrip();
        test_generated_source_clear_value_roundtrip();
        test_generated_source_clear_all_roundtrip();
        test_generated_source_row_column_clear_roundtrip();
        test_generated_source_row_column_range_clear_roundtrip();
        test_generated_source_row_column_erase_roundtrip();
        test_generated_source_row_column_range_erase_roundtrip();
        test_generated_source_append_row_roundtrip();
        test_generated_source_sparse_batch_replacement_roundtrip();
        test_generated_source_sparse_value_batch_roundtrip();
        test_generated_source_row_column_replacement_roundtrip();
        test_generated_source_row_column_value_roundtrip();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
