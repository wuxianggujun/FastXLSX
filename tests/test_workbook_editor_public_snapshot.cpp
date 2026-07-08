#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include "zip_test_utils.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
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

void check_coordinate_batch_cleared_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened coordinate-batch clear output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened coordinate-batch clear output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened coordinate-batch clear Data output should keep the sheet clean");
    check(data.cell_count() == 5,
        "reopened coordinate-batch clear Data output should keep represented cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened coordinate-batch clear Data output should keep sparse bounds");
    check(data.get_cell("A1").text_value() == "alpha",
        "reopened coordinate-batch clear Data output should keep A1");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened coordinate-batch clear Data output should keep B1 blank");
    check(data.get_cell("A2").text_value() == "tail",
        "reopened coordinate-batch clear Data output should keep A2");
    check(data.get_cell("C2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened coordinate-batch clear Data output should keep C2 blank");
    const fastxlsx::CellValue d3 = data.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "reopened coordinate-batch clear Data output should keep D3 boolean");
    check(!data.try_cell("B2").has_value(),
        "reopened coordinate-batch clear Data output should not synthesize B2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 5 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_text_snapshot(all_cells[2], 2, 1, "tail") &&
            is_blank_snapshot(all_cells[3], 2, 3) &&
            is_boolean_snapshot(all_cells[4], 3, 4, true),
        "reopened coordinate-batch clear Data sparse_cells should expose final cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 2 &&
            is_text_snapshot(row_two[0], 2, 1, "tail") &&
            is_blank_snapshot(row_two[1], 2, 3),
        "reopened coordinate-batch clear Data row_cells should expose row two");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        data.column_cells(3);
    check(column_three.size() == 1 &&
            is_blank_snapshot(column_three[0], 2, 3),
        "reopened coordinate-batch clear Data column_cells should expose blank C2");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened coordinate-batch clear Audit sheet should remain copy-original");
}

void check_a1_range_cleared_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened A1-range clear output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened A1-range clear output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened A1-range clear Data output should keep the sheet clean");
    check(data.cell_count() == 5,
        "reopened A1-range clear Data output should keep represented cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened A1-range clear Data output should keep sparse bounds");
    check(data.get_cell("A1").text_value() == "alpha",
        "reopened A1-range clear Data output should keep A1");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened A1-range clear Data output should keep B1 blank");
    check(!data.try_cell("C1").has_value(),
        "reopened A1-range clear Data output should not synthesize C1");
    check(data.get_cell("A2").text_value() == "tail",
        "reopened A1-range clear Data output should keep A2");
    check(!data.try_cell("B2").has_value(),
        "reopened A1-range clear Data output should not synthesize B2");
    check(data.get_cell("C2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened A1-range clear Data output should keep C2 blank");
    const fastxlsx::CellValue d3 = data.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "reopened A1-range clear Data output should keep D3 boolean");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 5 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_text_snapshot(all_cells[2], 2, 1, "tail") &&
            is_blank_snapshot(all_cells[3], 2, 3) &&
            is_boolean_snapshot(all_cells[4], 3, 4, true),
        "reopened A1-range clear Data sparse_cells should expose final cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    check(row_one.size() == 2 &&
            is_text_snapshot(row_one[0], 1, 1, "alpha") &&
            is_blank_snapshot(row_one[1], 1, 2),
        "reopened A1-range clear Data row_cells should expose row one");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 2 &&
            is_text_snapshot(row_two[0], 2, 1, "tail") &&
            is_blank_snapshot(row_two[1], 2, 3),
        "reopened A1-range clear Data row_cells should expose row two");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        data.column_cells(3);
    check(column_three.size() == 1 &&
            is_blank_snapshot(column_three[0], 2, 3),
        "reopened A1-range clear Data column_cells should expose blank C2");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened A1-range clear Audit sheet should remain copy-original");
}

void check_coordinate_batch_erased_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened coordinate-batch erase output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened coordinate-batch erase output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened coordinate-batch erase Data output should keep the sheet clean");
    check(data.cell_count() == 3,
        "reopened coordinate-batch erase Data output should keep surviving cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened coordinate-batch erase Data output should keep sparse bounds");
    check(data.get_cell("A1").text_value() == "alpha",
        "reopened coordinate-batch erase Data output should keep A1");
    check(!data.try_cell("B1").has_value(),
        "reopened coordinate-batch erase Data output should omit B1");
    check(data.get_cell("A2").text_value() == "tail",
        "reopened coordinate-batch erase Data output should keep A2");
    check(!data.try_cell("C2").has_value(),
        "reopened coordinate-batch erase Data output should omit C2");
    const fastxlsx::CellValue d3 = data.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "reopened coordinate-batch erase Data output should keep D3 boolean");
    check(!data.try_cell("B2").has_value(),
        "reopened coordinate-batch erase Data output should not synthesize B2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 3 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_text_snapshot(all_cells[1], 2, 1, "tail") &&
            is_boolean_snapshot(all_cells[2], 3, 4, true),
        "reopened coordinate-batch erase Data sparse_cells should expose survivors");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 1 &&
            is_text_snapshot(row_two[0], 2, 1, "tail"),
        "reopened coordinate-batch erase Data row_cells should expose row two survivor");

    check(data.column_cells(3).empty(),
        "reopened coordinate-batch erase Data column_cells should omit erased C2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
        data.column_cells(4);
    check(column_four.size() == 1 &&
            is_boolean_snapshot(column_four[0], 3, 4, true),
        "reopened coordinate-batch erase Data column_cells should expose D3");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened coordinate-batch erase Audit sheet should remain copy-original");
}

void check_a1_range_erased_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened A1-range erase output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened A1-range erase output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened A1-range erase Data output should keep the sheet clean");
    check(data.cell_count() == 3,
        "reopened A1-range erase Data output should keep surviving cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened A1-range erase Data output should keep sparse bounds");
    check(data.get_cell("A1").text_value() == "alpha",
        "reopened A1-range erase Data output should keep A1");
    check(!data.try_cell("B1").has_value(),
        "reopened A1-range erase Data output should omit B1");
    check(!data.try_cell("C1").has_value(),
        "reopened A1-range erase Data output should not synthesize C1");
    check(data.get_cell("A2").text_value() == "tail",
        "reopened A1-range erase Data output should keep A2");
    check(!data.try_cell("B2").has_value(),
        "reopened A1-range erase Data output should not synthesize B2");
    check(!data.try_cell("C2").has_value(),
        "reopened A1-range erase Data output should omit C2");
    const fastxlsx::CellValue d3 = data.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "reopened A1-range erase Data output should keep D3 boolean");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 3 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_text_snapshot(all_cells[1], 2, 1, "tail") &&
            is_boolean_snapshot(all_cells[2], 3, 4, true),
        "reopened A1-range erase Data sparse_cells should expose survivors");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        data.row_cells(1);
    check(row_one.size() == 1 &&
            is_text_snapshot(row_one[0], 1, 1, "alpha"),
        "reopened A1-range erase Data row_cells should expose row one survivor");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 1 &&
            is_text_snapshot(row_two[0], 2, 1, "tail"),
        "reopened A1-range erase Data row_cells should expose row two survivor");

    check(data.column_cells(3).empty(),
        "reopened A1-range erase Data column_cells should omit erased C2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
        data.column_cells(4);
    check(column_four.size() == 1 &&
            is_boolean_snapshot(column_four[0], 3, 4, true),
        "reopened A1-range erase Data column_cells should expose D3");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened A1-range erase Audit sheet should remain copy-original");
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

void check_single_value_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened single-value output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened single-value output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened single-value Data output should keep the sheet clean");
    check(data.cell_count() == 5,
        "reopened single-value Data output should materialize final sparse cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened single-value Data output should expose final sparse bounds");
    check(data.get_cell("A1").text_value() == "alpha",
        "reopened single-value Data output should preserve non-target source A1");
    check(data.get_cell("B1").text_value() == "value-single-b1",
        "reopened single-value Data output should read updated source-backed B1");
    check(data.get_cell("A2").text_value() == "tail",
        "reopened single-value Data output should preserve non-target source A2");
    check(data.get_cell("C2").text_value() == "value-single-c2",
        "reopened single-value Data output should read inserted C2");
    const fastxlsx::CellValue d3 = data.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "reopened single-value Data output should preserve non-target dirty D3");
    check(!data.try_cell("B2").has_value() &&
            !data.try_cell("C1").has_value(),
        "reopened single-value Data output should not synthesize missing cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 5 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_text_snapshot(all_cells[1], 1, 2, "value-single-b1") &&
            is_text_snapshot(all_cells[2], 2, 1, "tail") &&
            is_text_snapshot(all_cells[3], 2, 3, "value-single-c2") &&
            is_boolean_snapshot(all_cells[4], 3, 4, true),
        "reopened single-value Data sparse_cells should expose final row-major cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 2 &&
            is_text_snapshot(row_two[0], 2, 1, "tail") &&
            is_text_snapshot(row_two[1], 2, 3, "value-single-c2"),
        "reopened single-value Data row_cells should expose sparse row two");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        data.column_cells(3);
    check(column_three.size() == 1 &&
            is_text_snapshot(column_three[0], 2, 3, "value-single-c2"),
        "reopened single-value Data column_cells should expose inserted C2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
        data.column_cells(4);
    check(column_four.size() == 1 &&
            is_boolean_snapshot(column_four[0], 3, 4, true),
        "reopened single-value Data column_cells should expose preserved dirty D3");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened single-value Audit sheet should remain copy-original");
}

void check_contains_cell_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened contains-cell output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened contains-cell output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened contains-cell Data output should keep the sheet clean");
    check(data.cell_count() == 3,
        "reopened contains-cell Data output should materialize final sparse cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 3),
        "reopened contains-cell Data output should expose final sparse bounds");
    check(data.contains_cell(1, 1) &&
            data.contains_cell("B1") &&
            data.contains_cell(3, 3),
        "reopened contains-cell Data output should report represented cells");
    check(!data.contains_cell("A2") &&
            !data.contains_cell(2, 3) &&
            !data.contains_cell("B2"),
        "reopened contains-cell Data output should report erased and old shifted coordinates as missing");
    check(data.get_cell("A1").text_value() == "alpha",
        "reopened contains-cell Data output should keep source-backed A1");
    check(data.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "reopened contains-cell Data output should keep explicit B1 blank");
    check(data.get_cell("C3").text_value() == "contains-shifted",
        "reopened contains-cell Data output should read shifted inserted C3");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 3 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_text_snapshot(all_cells[2], 3, 3, "contains-shifted"),
        "reopened contains-cell Data sparse_cells should expose final row-major cells");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened contains-cell Audit sheet should remain copy-original");
}

void check_inspection_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened inspection output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened inspection output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(data.name() == "Data",
        "reopened inspection Data output should expose the worksheet name");
    check(!data.has_pending_changes(),
        "reopened inspection Data output should keep the sheet clean");
    check(data.cell_count() == 5,
        "reopened inspection Data output should materialize final sparse cells");
    check(data.estimated_memory_usage() > 0,
        "reopened inspection Data output should expose a non-zero memory estimate");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened inspection Data output should expose final sparse bounds");
    check(data.contains_cell("A1") &&
            data.contains_cell(1, 2) &&
            data.contains_cell("A2") &&
            data.contains_cell("D2") &&
            data.contains_cell(3, 3),
        "reopened inspection Data output should report represented cells");
    check(!data.contains_cell("B2") &&
            !data.contains_cell("C2") &&
            !data.contains_cell("D3"),
        "reopened inspection Data output should report sparse gaps as missing");

    const std::optional<fastxlsx::CellValue> a1 = data.try_cell("A1");
    check(a1.has_value() &&
            a1->kind() == fastxlsx::CellValueKind::Text &&
            a1->text_value() == "alpha",
        "reopened inspection try_cell should read source-backed A1");
    check(data.get_cell("B1").number_value() == 5.0,
        "reopened inspection get_cell should read updated B1");
    check(data.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "reopened inspection get_cell should read cleared A2 as blank");
    const fastxlsx::CellValue d2 = data.get_cell("D2");
    check(d2.kind() == fastxlsx::CellValueKind::Formula &&
            d2.text_value() == "A1+B1",
        "reopened inspection get_cell should read inserted D2 formula");
    const std::optional<fastxlsx::CellValue> c3 = data.try_cell("C3");
    check(c3.has_value() &&
            c3->kind() == fastxlsx::CellValueKind::Text &&
            c3->text_value() == "inspection-c3",
        "reopened inspection try_cell should read inserted C3");
    check(!data.try_cell("B2").has_value(),
        "reopened inspection try_cell should skip missing B2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> requested =
        data.sparse_cells({
            fastxlsx::WorksheetCellReference {2, 4},
            fastxlsx::WorksheetCellReference {2, 2},
            fastxlsx::WorksheetCellReference {2, 1},
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::WorksheetCellReference {2, 4},
        });
    check(requested.size() == 5 &&
            is_formula_snapshot(requested[0], 2, 4, "A1+B1") &&
            is_blank_snapshot(requested[1], 2, 1) &&
            is_text_snapshot(requested[2], 1, 1, "alpha") &&
            is_text_snapshot(requested[3], 3, 3, "inspection-c3") &&
            is_formula_snapshot(requested[4], 2, 4, "A1+B1"),
        "reopened inspection sparse_cells batch should preserve requested order, duplicates, and missing skips");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        data.row_cells(2);
    check(row_two.size() == 2 &&
            is_blank_snapshot(row_two[0], 2, 1) &&
            is_formula_snapshot(row_two[1], 2, 4, "A1+B1"),
        "reopened inspection row_cells should expose blank A2 and formula D2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
        data.column_cells(4);
    check(column_four.size() == 1 &&
            is_formula_snapshot(column_four[0], 2, 4, "A1+B1"),
        "reopened inspection column_cells should expose formula D2");
    check(!data.has_pending_changes() && !reopened.has_pending_changes(),
        "reopened inspection reads should keep the session clean");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened inspection Audit sheet should remain copy-original");
}

bool is_catalog_entry(
    const fastxlsx::WorkbookEditorWorksheetCatalogEntry& entry,
    std::string_view source_name,
    std::string_view planned_name,
    bool renamed)
{
    return entry.source_name == source_name &&
        entry.planned_name == planned_name &&
        entry.renamed == renamed;
}

void check_generated_source_catalog(const fastxlsx::WorkbookEditor& editor)
{
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
        editor.worksheet_catalog();
    check(catalog.size() == 2 &&
            is_catalog_entry(catalog[0], "Data", "Data", false) &&
            is_catalog_entry(catalog[1], "Audit", "Audit", false),
        "catalog inspection diagnostics should expose the source-to-planned catalog");
}

void check_clean_catalog_materialized_diagnostics(
    const fastxlsx::WorkbookEditor& editor)
{
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "catalog inspection clean diagnostics should not expose dirty materialized sessions");
    check(editor.pending_worksheet_edits().empty(),
        "catalog inspection clean diagnostics should not expose worksheet edit summaries");
    check_generated_source_catalog(editor);
}

void check_dirty_catalog_materialized_diagnostics(
    const fastxlsx::WorkbookEditor& editor,
    std::size_t expected_memory)
{
    check(editor.pending_materialized_worksheet_names() ==
            std::vector<std::string>({"Data"}),
        "catalog inspection dirty diagnostics should expose Data as dirty");
    check(editor.pending_materialized_cell_count() == 4,
        "catalog inspection dirty diagnostics should expose dirty sparse cell count");
    check(editor.estimated_pending_materialized_memory_usage() == expected_memory,
        "catalog inspection dirty diagnostics should expose dirty sparse memory");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        "catalog inspection dirty diagnostics should expose one worksheet summary");
    const fastxlsx::WorkbookEditorWorksheetEditSummary& summary = summaries[0];
    check(summary.source_name == "Data" &&
            summary.planned_name == "Data" &&
            !summary.renamed,
        "catalog inspection dirty summary should keep source/planned Data names");
    check(!summary.sheet_data_replaced &&
            summary.replacement_cell_count == 0 &&
            summary.estimated_replacement_memory_usage == 0,
        "catalog inspection dirty summary should not expose whole-sheet replacement state");
    check(!summary.targeted_cells_replaced &&
            summary.targeted_cell_replacement_count == 0 &&
            summary.estimated_targeted_cell_replacement_xml_bytes == 0,
        "catalog inspection dirty summary should not expose targeted-cell replacement state");
    check(summary.materialized_dirty &&
            summary.materialized_cell_count == 4 &&
            summary.estimated_materialized_memory_usage == expected_memory,
        "catalog inspection dirty summary should expose materialized Data state");
    check_generated_source_catalog(editor);
}

void check_catalog_inspection_output(
    const std::filesystem::path& output, bool has_dirty_edit)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened catalog inspection output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened catalog inspection output should not expose pending handoffs");
    check(reopened.worksheet_names() == std::vector<std::string>({"Data", "Audit"}),
        "reopened catalog inspection output should expose planned worksheet names");
    check(reopened.source_worksheet_names() ==
            std::vector<std::string>({"Data", "Audit"}),
        "reopened catalog inspection output should expose source worksheet names");
    check(reopened.has_worksheet("Data") &&
            reopened.has_worksheet("Audit") &&
            !reopened.has_worksheet("Missing"),
        "reopened catalog inspection output should answer planned worksheet probes");
    check(reopened.has_source_worksheet("Data") &&
            reopened.has_source_worksheet("Audit") &&
            !reopened.has_source_worksheet("Missing"),
        "reopened catalog inspection output should answer source worksheet probes");

    std::optional<fastxlsx::WorksheetEditor> data =
        reopened.try_worksheet("Data");
    std::optional<fastxlsx::WorksheetEditor> audit =
        reopened.try_worksheet("Audit");
    check(data.has_value() && audit.has_value(),
        "reopened catalog inspection output should reacquire existing sheets");
    check(!reopened.try_worksheet("Missing").has_value(),
        "reopened catalog inspection output should return nullopt for a missing sheet");
    check(data->name() == "Data" && audit->name() == "Audit",
        "reopened catalog inspection output should keep handle names");
    check(!data->has_pending_changes() &&
            !audit->has_pending_changes() &&
            !reopened.has_pending_changes(),
        "reopened catalog inspection reads should keep sessions clean");
    check_clean_catalog_materialized_diagnostics(reopened);

    const std::size_t expected_cells = has_dirty_edit ? 4u : 3u;
    check(data->cell_count() == expected_cells,
        "reopened catalog inspection Data output should expose expected sparse cell count");
    check(is_used_range(data->used_range(), 1, 1, 2, has_dirty_edit ? 3u : 2u),
        "reopened catalog inspection Data output should expose expected sparse bounds");
    check(data->get_cell("A1").text_value() == "alpha" &&
            data->get_cell("B1").number_value() == 2.0 &&
            data->get_cell("A2").text_value() == "tail",
        "reopened catalog inspection Data output should preserve source-backed cells");
    if (has_dirty_edit) {
        check(data->get_cell("C2").text_value() == "catalog-dirty",
            "reopened catalog inspection Data output should expose the saved dirty cell");
    } else {
        check(!data->try_cell("C2").has_value(),
            "reopened catalog inspection clean output should not synthesize C2");
    }
    check(audit->cell_count() == 1 &&
            audit->get_cell("A1").text_value() == "untouched",
        "reopened catalog inspection Audit sheet should remain copy-original");
    check(!data->has_pending_changes() &&
            !audit->has_pending_changes() &&
            !reopened.has_pending_changes(),
        "reopened catalog inspection cell reads should keep sessions clean");
}

void check_sparse_initializer_list_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened sparse initializer-list output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened sparse initializer-list output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened sparse initializer-list Data output should keep the sheet clean");
    check(data.cell_count() == 4,
        "reopened sparse initializer-list Data output should materialize final sparse cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened sparse initializer-list Data output should expose final sparse bounds");

    const std::vector<fastxlsx::WorksheetCellSnapshot> requested =
        data.sparse_cells({
            fastxlsx::WorksheetCellReference {3, 4},
            fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::WorksheetCellReference {2, 1},
            fastxlsx::WorksheetCellReference {3, 4},
            fastxlsx::WorksheetCellReference {1, 3},
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {4, 4},
        });
    check(requested.size() == 5 &&
            is_formula_snapshot(requested[0], 3, 4, "A1+C1") &&
            is_blank_snapshot(requested[1], 1, 2) &&
            is_formula_snapshot(requested[2], 3, 4, "A1+C1") &&
            is_number_snapshot(requested[3], 1, 3, 9.0) &&
            is_text_snapshot(requested[4], 1, 1, "alpha"),
        "reopened sparse initializer-list batch should preserve order, duplicates, and missing skips");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 4 &&
            is_text_snapshot(all_cells[0], 1, 1, "alpha") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_number_snapshot(all_cells[2], 1, 3, 9.0) &&
            is_formula_snapshot(all_cells[3], 3, 4, "A1+C1"),
        "reopened sparse initializer-list Data sparse_cells should expose final row-major cells");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened sparse initializer-list Audit sheet should remain copy-original");
}

void check_span_batch_output(const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened span-batch output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened span-batch output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened span-batch Data output should keep the sheet clean");
    check(data.cell_count() == 4,
        "reopened span-batch Data output should materialize final sparse cells");
    check(is_used_range(data.used_range(), 1, 1, 3, 4),
        "reopened span-batch Data output should expose final sparse bounds");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == 4 &&
            is_text_snapshot(all_cells[0], 1, 1, "span-full-a") &&
            is_blank_snapshot(all_cells[1], 1, 2) &&
            is_blank_snapshot(all_cells[2], 3, 3) &&
            is_formula_snapshot(all_cells[3], 3, 4, "A1+B1"),
        "reopened span-batch Data sparse_cells should expose final row-major cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
        data.row_cells(3);
    check(row_three.size() == 2 &&
            is_blank_snapshot(row_three[0], 3, 3) &&
            is_formula_snapshot(row_three[1], 3, 4, "A1+B1"),
        "reopened span-batch row_cells should expose cleared C3 and retained D3");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
        data.column_cells(2);
    check(column_two.size() == 1 &&
            is_blank_snapshot(column_two[0], 1, 2),
        "reopened span-batch column_cells should expose blank B1 only");
    check(!data.contains_cell("A2") &&
            !data.contains_cell("B2") &&
            !data.contains_cell("E5"),
        "reopened span-batch output should keep erased and missing span targets absent");

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened span-batch Audit sheet should remain copy-original");
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

struct ExpectedShiftCell {
    fastxlsx::WorksheetCellReference reference {};
    fastxlsx::CellValueKind kind = fastxlsx::CellValueKind::Blank;
    std::string text;
    double number = 0.0;
    bool boolean = false;
};

ExpectedShiftCell expected_shift_text(
    std::uint32_t row, std::uint32_t column, std::string_view text)
{
    return ExpectedShiftCell {
        fastxlsx::WorksheetCellReference {row, column},
        fastxlsx::CellValueKind::Text,
        std::string(text),
    };
}

ExpectedShiftCell expected_shift_number(
    std::uint32_t row, std::uint32_t column, double number)
{
    ExpectedShiftCell expected;
    expected.reference = fastxlsx::WorksheetCellReference {row, column};
    expected.kind = fastxlsx::CellValueKind::Number;
    expected.number = number;
    return expected;
}

ExpectedShiftCell expected_shift_boolean(
    std::uint32_t row, std::uint32_t column, bool boolean)
{
    ExpectedShiftCell expected;
    expected.reference = fastxlsx::WorksheetCellReference {row, column};
    expected.kind = fastxlsx::CellValueKind::Boolean;
    expected.boolean = boolean;
    return expected;
}

bool matches_expected_shift_value(
    const fastxlsx::CellValue& value,
    const ExpectedShiftCell& expected)
{
    if (value.kind() != expected.kind) {
        return false;
    }
    switch (expected.kind) {
    case fastxlsx::CellValueKind::Text:
        return value.text_value() == expected.text;
    case fastxlsx::CellValueKind::Number:
        return value.number_value() == expected.number;
    case fastxlsx::CellValueKind::Boolean:
        return value.boolean_value() == expected.boolean;
    default:
        return false;
    }
}

bool matches_expected_shift_snapshot(
    const fastxlsx::WorksheetCellSnapshot& snapshot,
    const ExpectedShiftCell& expected)
{
    return snapshot.reference.row == expected.reference.row &&
        snapshot.reference.column == expected.reference.column &&
        matches_expected_shift_value(snapshot.value, expected);
}

void check_structural_shift_output(
    const std::filesystem::path& output,
    const fastxlsx::CellRange& used_range,
    const std::vector<ExpectedShiftCell>& expected_cells,
    const std::vector<fastxlsx::WorksheetCellReference>& absent_cells)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(!reopened.has_pending_changes(),
        "reopened structural-shift output should start clean");
    check(reopened.pending_change_count() == 0,
        "reopened structural-shift output should not expose pending handoffs");

    fastxlsx::WorksheetEditor data = reopened.worksheet("Data");
    check(!data.has_pending_changes(),
        "reopened structural-shift Data output should keep the sheet clean");
    check(data.cell_count() == expected_cells.size(),
        "reopened structural-shift Data output should materialize final sparse cells");
    check(is_used_range(data.used_range(), used_range.first_row,
            used_range.first_column, used_range.last_row, used_range.last_column),
        "reopened structural-shift Data output should expose final sparse bounds");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        data.sparse_cells();
    check(all_cells.size() == expected_cells.size(),
        "reopened structural-shift sparse_cells should expose final cell count");
    for (std::size_t i = 0; i < expected_cells.size(); ++i) {
        check(matches_expected_shift_snapshot(all_cells[i], expected_cells[i]),
            "reopened structural-shift sparse_cells should expose expected row-major cells");
        check(matches_expected_shift_value(
                data.get_cell(expected_cells[i].reference.row,
                    expected_cells[i].reference.column),
                expected_cells[i]),
            "reopened structural-shift get_cell should read expected cell value");
    }

    for (const fastxlsx::WorksheetCellReference& absent : absent_cells) {
        check(!data.try_cell(absent.row, absent.column).has_value(),
            "reopened structural-shift output should keep old or missing coordinates absent");
    }

    fastxlsx::WorksheetEditor audit = reopened.worksheet("Audit");
    check(audit.cell_count() == 1 &&
            audit.get_cell("A1").text_value() == "untouched",
        "reopened structural-shift Audit sheet should remain copy-original");
}

void check_invalid_snapshot_reads_preserve_diagnostics(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::optional<std::string>& mutation_error)
{
    const std::size_t cell_count_before = sheet.cell_count();
    const std::size_t memory_before = sheet.estimated_memory_usage();
    const bool sheet_dirty_before = sheet.has_pending_changes();
    const bool editor_dirty_before = editor.has_pending_changes();
    const std::size_t pending_change_count_before =
        editor.pending_change_count();
    const std::vector<std::string> pending_materialized_names_before =
        editor.pending_materialized_worksheet_names();
    const std::size_t pending_materialized_cell_count_before =
        editor.pending_materialized_cell_count();
    const std::size_t pending_materialized_memory_before =
        editor.estimated_pending_materialized_memory_usage();
    const std::size_t pending_worksheet_edit_count_before =
        editor.pending_worksheet_edits().size();

    check(threw_fastxlsx_error([&sheet] { (void)sheet.try_cell(0, 1); }),
        "try_cell(row, column) should reject row zero");
    check(threw_fastxlsx_error([&sheet] { (void)sheet.try_cell("a1"); }),
        "try_cell(A1) should reject lowercase references");
    check(editor.last_edit_error() == mutation_error,
        "try_cell invalid reads should preserve prior last_edit_error");
    check(threw_fastxlsx_error([&sheet] { (void)sheet.get_cell(1, 0); }),
        "get_cell(row, column) should reject column zero");
    check(threw_fastxlsx_error([&sheet] { (void)sheet.get_cell("B2"); }),
        "get_cell(A1) should reject valid but missing cells");
    check(editor.last_edit_error() == mutation_error,
        "get_cell read failures should preserve prior last_edit_error");
    check(threw_fastxlsx_error([&sheet] { (void)sheet.contains_cell(0, 1); }),
        "contains_cell(row, column) should reject row zero");
    check(threw_fastxlsx_error([&sheet] { (void)sheet.contains_cell("A1:B2"); }),
        "contains_cell(A1) should reject ranges");
    check(editor.last_edit_error() == mutation_error,
        "contains_cell invalid reads should preserve prior last_edit_error");
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
    check(sheet.cell_count() == cell_count_before,
        "invalid read failures should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == memory_before,
        "invalid read failures should preserve sparse-store memory estimate");
    check(sheet.has_pending_changes() == sheet_dirty_before &&
            editor.has_pending_changes() == editor_dirty_before,
        "invalid read failures should preserve dirty flags");
    check(editor.pending_change_count() == pending_change_count_before,
        "invalid read failures should preserve pending change count");
    check(editor.pending_materialized_worksheet_names() ==
            pending_materialized_names_before &&
            editor.pending_materialized_cell_count() ==
                pending_materialized_cell_count_before &&
            editor.estimated_pending_materialized_memory_usage() ==
                pending_materialized_memory_before,
        "invalid read failures should preserve materialized diagnostics");
    check(editor.pending_worksheet_edits().size() ==
            pending_worksheet_edit_count_before,
        "invalid read failures should preserve worksheet summary diagnostics");
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
    check_invalid_snapshot_reads_preserve_diagnostics(
        editor, sheet, std::nullopt);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "invalid dirty snapshot reads should preserve dirty flags");
    check(editor.pending_materialized_cell_count() == 4,
        "invalid dirty snapshot reads should preserve dirty materialized cell count");

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

void test_generated_source_coordinate_batch_clear_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-clear-batch-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-clear-batch-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C2", fastxlsx::CellValue::text("clear-batch-c2"));
    sheet.set_cell("D3", fastxlsx::CellValue::boolean(true));
    check(sheet.cell_count() == 5,
        "coordinate-batch clear setup should add C2 and D3 sparse cells");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "coordinate-batch clear setup should expand sparse bounds");

    sheet.clear_cell_values({
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {2, 3},
        fastxlsx::WorksheetCellReference {2, 2},
        fastxlsx::WorksheetCellReference {2, 3},
    });
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "coordinate-batch clear should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "coordinate-batch clear should keep represented sparse cells");
    check(editor.pending_materialized_cell_count() == 5,
        "coordinate-batch clear should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "coordinate-batch clear should keep sparse bounds");
    check(sheet.get_cell("A1").text_value() == "alpha",
        "coordinate-batch clear should keep non-target source A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "coordinate-batch clear should convert source B1 to blank");
    check(sheet.get_cell("A2").text_value() == "tail",
        "coordinate-batch clear should keep non-target source A2");
    check(sheet.get_cell("C2").kind() == fastxlsx::CellValueKind::Blank,
        "coordinate-batch clear should convert dirty C2 to blank");
    const fastxlsx::CellValue d3 = sheet.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "coordinate-batch clear should keep non-target dirty D3");
    check(!sheet.try_cell("B2").has_value(),
        "coordinate-batch clear should not synthesize missing B2");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "coordinate-batch clear save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "coordinate-batch clear save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "coordinate-batch clear save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "coordinate-batch clear save_as should write final worksheet dimension");
    check_contains(data_xml, "alpha",
        "coordinate-batch clear save_as should keep A1 text");
    check_contains(data_xml, "tail",
        "coordinate-batch clear save_as should keep A2 text");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "coordinate-batch clear save_as should write blank B1");
    check_contains(data_xml, R"(<c r="C2"/>)",
        "coordinate-batch clear save_as should write blank C2");
    check_contains(data_xml, R"(<c r="D3" t="b"><v>1</v></c>)",
        "coordinate-batch clear save_as should preserve D3 boolean");
    check_not_contains(data_xml, R"(r="B2")",
        "coordinate-batch clear save_as should not synthesize B2");
    check_not_contains(data_xml, "clear-batch-c2",
        "coordinate-batch clear save_as should omit cleared dirty C2 text");
    check_not_contains(data_xml, "<v>2",
        "coordinate-batch clear save_as should omit cleared source B1 number");
    check_coordinate_batch_cleared_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean coordinate-batch clear no-op save should keep output entries stable");
    check_coordinate_batch_cleared_output(noop_output);
}

void test_generated_source_a1_range_clear_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-clear-a1-range-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-clear-a1-range-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C2", fastxlsx::CellValue::text("clear-a1-range-c2"));
    sheet.set_cell("D3", fastxlsx::CellValue::boolean(true));
    check(sheet.cell_count() == 5,
        "A1-range clear setup should add C2 and D3 sparse cells");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "A1-range clear setup should expand sparse bounds");

    sheet.clear_cell_values("B1:C2");
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "A1-range clear should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "A1-range clear should keep represented sparse cells");
    check(editor.pending_materialized_cell_count() == 5,
        "A1-range clear should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "A1-range clear should keep sparse bounds");
    check(sheet.get_cell("A1").text_value() == "alpha",
        "A1-range clear should keep non-target source A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "A1-range clear should convert source B1 to blank");
    check(!sheet.try_cell("C1").has_value(),
        "A1-range clear should not synthesize missing C1");
    check(sheet.get_cell("A2").text_value() == "tail",
        "A1-range clear should keep non-target source A2");
    check(!sheet.try_cell("B2").has_value(),
        "A1-range clear should not synthesize missing B2");
    check(sheet.get_cell("C2").kind() == fastxlsx::CellValueKind::Blank,
        "A1-range clear should convert dirty C2 to blank");
    const fastxlsx::CellValue d3 = sheet.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "A1-range clear should keep non-target dirty D3");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "A1-range clear save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "A1-range clear save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "A1-range clear save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "A1-range clear save_as should write final worksheet dimension");
    check_contains(data_xml, "alpha",
        "A1-range clear save_as should keep A1 text");
    check_contains(data_xml, "tail",
        "A1-range clear save_as should keep A2 text");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "A1-range clear save_as should write blank B1");
    check_contains(data_xml, R"(<c r="C2"/>)",
        "A1-range clear save_as should write blank C2");
    check_contains(data_xml, R"(<c r="D3" t="b"><v>1</v></c>)",
        "A1-range clear save_as should preserve D3 boolean");
    check_not_contains(data_xml, R"(r="C1")",
        "A1-range clear save_as should not synthesize C1");
    check_not_contains(data_xml, R"(r="B2")",
        "A1-range clear save_as should not synthesize B2");
    check_not_contains(data_xml, "clear-a1-range-c2",
        "A1-range clear save_as should omit cleared dirty C2 text");
    check_not_contains(data_xml, "<v>2",
        "A1-range clear save_as should omit cleared source B1 number");
    check_a1_range_cleared_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean A1-range clear no-op save should keep output entries stable");
    check_a1_range_cleared_output(noop_output);
}

void test_generated_source_coordinate_batch_erase_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-erase-batch-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-erase-batch-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C2", fastxlsx::CellValue::text("erase-batch-c2"));
    sheet.set_cell("D3", fastxlsx::CellValue::boolean(true));
    check(sheet.cell_count() == 5,
        "coordinate-batch erase setup should add C2 and D3 sparse cells");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "coordinate-batch erase setup should expand sparse bounds");

    sheet.erase_cells({
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {2, 3},
        fastxlsx::WorksheetCellReference {2, 2},
        fastxlsx::WorksheetCellReference {2, 3},
    });
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "coordinate-batch erase should dirty the materialized session");
    check(sheet.cell_count() == 3,
        "coordinate-batch erase should remove target sparse cells");
    check(editor.pending_materialized_cell_count() == 3,
        "coordinate-batch erase should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "coordinate-batch erase should keep sparse bounds from the surviving D3");
    check(sheet.get_cell("A1").text_value() == "alpha",
        "coordinate-batch erase should keep non-target source A1");
    check(!sheet.try_cell("B1").has_value(),
        "coordinate-batch erase should remove source-backed B1");
    check(sheet.get_cell("A2").text_value() == "tail",
        "coordinate-batch erase should keep non-target source A2");
    check(!sheet.try_cell("C2").has_value(),
        "coordinate-batch erase should remove dirty C2");
    const fastxlsx::CellValue d3 = sheet.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "coordinate-batch erase should keep non-target dirty D3");
    check(!sheet.try_cell("B2").has_value(),
        "coordinate-batch erase should not synthesize missing B2");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "coordinate-batch erase save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "coordinate-batch erase save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "coordinate-batch erase save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "coordinate-batch erase save_as should write final worksheet dimension");
    check_contains(data_xml, "alpha",
        "coordinate-batch erase save_as should keep A1 text");
    check_contains(data_xml, "tail",
        "coordinate-batch erase save_as should keep A2 text");
    check_contains(data_xml, R"(<c r="D3" t="b"><v>1</v></c>)",
        "coordinate-batch erase save_as should preserve D3 boolean");
    check_not_contains(data_xml, R"(r="B1")",
        "coordinate-batch erase save_as should omit erased B1");
    check_not_contains(data_xml, R"(r="C2")",
        "coordinate-batch erase save_as should omit erased C2");
    check_not_contains(data_xml, R"(r="B2")",
        "coordinate-batch erase save_as should not synthesize B2");
    check_not_contains(data_xml, "erase-batch-c2",
        "coordinate-batch erase save_as should omit erased dirty C2 text");
    check_not_contains(data_xml, "<v>2",
        "coordinate-batch erase save_as should omit erased source B1 number");
    check_coordinate_batch_erased_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean coordinate-batch erase no-op save should keep output entries stable");
    check_coordinate_batch_erased_output(noop_output);
}

void test_generated_source_a1_range_erase_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-erase-a1-range-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-erase-a1-range-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C2", fastxlsx::CellValue::text("erase-a1-range-c2"));
    sheet.set_cell("D3", fastxlsx::CellValue::boolean(true));
    check(sheet.cell_count() == 5,
        "A1-range erase setup should add C2 and D3 sparse cells");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "A1-range erase setup should expand sparse bounds");

    sheet.erase_cells("B1:C2");
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "A1-range erase should dirty the materialized session");
    check(sheet.cell_count() == 3,
        "A1-range erase should remove target sparse cells");
    check(editor.pending_materialized_cell_count() == 3,
        "A1-range erase should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "A1-range erase should keep sparse bounds from the surviving D3");
    check(sheet.get_cell("A1").text_value() == "alpha",
        "A1-range erase should keep non-target source A1");
    check(!sheet.try_cell("B1").has_value(),
        "A1-range erase should remove source-backed B1");
    check(!sheet.try_cell("C1").has_value(),
        "A1-range erase should not synthesize missing C1");
    check(sheet.get_cell("A2").text_value() == "tail",
        "A1-range erase should keep non-target source A2");
    check(!sheet.try_cell("B2").has_value(),
        "A1-range erase should not synthesize missing B2");
    check(!sheet.try_cell("C2").has_value(),
        "A1-range erase should remove dirty C2");
    const fastxlsx::CellValue d3 = sheet.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "A1-range erase should keep non-target dirty D3");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "A1-range erase save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "A1-range erase save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "A1-range erase save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "A1-range erase save_as should write final worksheet dimension");
    check_contains(data_xml, "alpha",
        "A1-range erase save_as should keep A1 text");
    check_contains(data_xml, "tail",
        "A1-range erase save_as should keep A2 text");
    check_contains(data_xml, R"(<c r="D3" t="b"><v>1</v></c>)",
        "A1-range erase save_as should preserve D3 boolean");
    check_not_contains(data_xml, R"(r="B1")",
        "A1-range erase save_as should omit erased B1");
    check_not_contains(data_xml, R"(r="C1")",
        "A1-range erase save_as should not synthesize C1");
    check_not_contains(data_xml, R"(r="B2")",
        "A1-range erase save_as should not synthesize B2");
    check_not_contains(data_xml, R"(r="C2")",
        "A1-range erase save_as should omit erased C2");
    check_not_contains(data_xml, "erase-a1-range-c2",
        "A1-range erase save_as should omit erased dirty C2 text");
    check_not_contains(data_xml, "<v>2",
        "A1-range erase save_as should omit erased source B1 number");
    check_a1_range_erased_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean A1-range erase no-op save should keep output entries stable");
    check_a1_range_erased_output(noop_output);
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

void test_generated_source_single_value_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-single-value-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-single-value-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("D3", fastxlsx::CellValue::boolean(true));
    check(sheet.cell_count() == 4,
        "single-value setup should add one non-target dirty cell");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "single-value setup should expand sparse bounds");

    sheet.set_cell_value(1, 2,
        fastxlsx::CellValue::text("value-single-b1"));
    sheet.set_cell_value("C2",
        fastxlsx::CellValue::text("value-single-c2"));
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "single-value edits should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "single-value edits should expose final represented sparse cells");
    check(editor.pending_materialized_cell_count() == 5,
        "single-value edits should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "single-value edits should expose final sparse bounds");
    check(sheet.get_cell("A1").text_value() == "alpha",
        "single-value edits should preserve non-target source A1");
    check(sheet.get_cell("B1").text_value() == "value-single-b1",
        "single-value edits should update source-backed B1");
    check(sheet.get_cell("A2").text_value() == "tail",
        "single-value edits should preserve non-target source A2");
    check(sheet.get_cell("C2").text_value() == "value-single-c2",
        "single-value edits should insert C2 through the A1 overload");
    const fastxlsx::CellValue d3 = sheet.get_cell("D3");
    check(d3.kind() == fastxlsx::CellValueKind::Boolean &&
            d3.boolean_value(),
        "single-value edits should preserve non-target dirty D3");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "single-value save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "single-value save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "single-value save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "single-value save_as should write final worksheet dimension");
    check_contains(data_xml, "alpha",
        "single-value save_as should keep non-target A1 text");
    check_contains(data_xml, "value-single-b1",
        "single-value save_as should write updated B1 text");
    check_contains(data_xml, "tail",
        "single-value save_as should keep non-target A2 text");
    check_contains(data_xml, "value-single-c2",
        "single-value save_as should write inserted C2 text");
    check_contains(data_xml, R"(<c r="D3" t="b"><v>1</v></c>)",
        "single-value save_as should preserve non-target dirty D3");
    check_not_contains(data_xml, "<v>2",
        "single-value save_as should omit overwritten source B1 number");
    check_not_contains(data_xml, R"(r="B2")",
        "single-value save_as should not synthesize missing B2");
    check_single_value_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean single-value no-op save should keep output entries stable");
    check_single_value_output(noop_output);
}

void test_generated_source_contains_cell_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-contains-cell-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-contains-cell-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    check(sheet.contains_cell(1, 1) &&
            sheet.contains_cell("B1") &&
            sheet.contains_cell("A2"),
        "contains-cell roundtrip should report source-backed records");
    check(!sheet.contains_cell("B2") &&
            !sheet.contains_cell(3, 3),
        "contains-cell roundtrip should report missing source cells as absent");
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "contains-cell source reads should not dirty the materialized session");

    sheet.set_cell("C2", fastxlsx::CellValue::text("contains-shifted"));
    sheet.clear_cell_value("B1");
    sheet.erase_cell("A2");
    sheet.insert_rows(2, 1);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "contains-cell mutations should dirty the materialized session");
    check(sheet.cell_count() == 3,
        "contains-cell mutations should keep only represented final cells");
    check(editor.pending_materialized_cell_count() == 3,
        "contains-cell mutations should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 3),
        "contains-cell mutations should expose final sparse bounds");
    check(sheet.contains_cell("A1") &&
            sheet.contains_cell(1, 2) &&
            sheet.contains_cell("C3"),
        "contains-cell mutations should report source, blank, and shifted cells");
    check(!sheet.contains_cell("A2") &&
            !sheet.contains_cell("C2") &&
            !sheet.contains_cell("B2"),
        "contains-cell mutations should report erased, old shifted, and missing cells as absent");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "contains-cell mutations should keep B1 as an explicit blank");
    check(sheet.get_cell("C3").text_value() == "contains-shifted",
        "contains-cell mutations should move inserted C2 down to C3");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "contains-cell save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "contains-cell save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "contains-cell save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:C3\"",
        "contains-cell save_as should write final worksheet dimension");
    check_contains(data_xml, "alpha",
        "contains-cell save_as should keep source-backed A1");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "contains-cell save_as should write explicit B1 blank");
    check_contains(data_xml, R"(r="C3")",
        "contains-cell save_as should write shifted C3 coordinate");
    check_contains(data_xml, "contains-shifted",
        "contains-cell save_as should write shifted inserted text");
    check_not_contains(data_xml, "tail",
        "contains-cell save_as should omit erased source A2 text");
    check_not_contains(data_xml, R"(r="A2")",
        "contains-cell save_as should omit erased A2 coordinate");
    check_not_contains(data_xml, R"(r="C2")",
        "contains-cell save_as should omit old shifted C2 coordinate");
    check_contains_cell_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean contains-cell no-op save should keep output entries stable");
    check_contains_cell_output(noop_output);
}

void test_generated_source_inspection_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-inspection-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-inspection-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(sheet.name() == "Data",
        "inspection roundtrip should expose the worksheet name");
    check_initial_snapshots(sheet);
    const std::size_t source_memory = sheet.estimated_memory_usage();
    check(source_memory > 0,
        "inspection source reads should expose a non-zero memory estimate");
    check(sheet.try_cell("A1").has_value() &&
            sheet.try_cell("A1")->text_value() == "alpha",
        "inspection source reads should read source-backed A1");
    check(sheet.get_cell("B1").number_value() == 2.0,
        "inspection source reads should read source-backed B1");
    check(sheet.contains_cell("A2") &&
            !sheet.contains_cell("C3") &&
            !sheet.try_cell("B2").has_value(),
        "inspection source reads should report represented and missing cells");
    const std::vector<fastxlsx::WorksheetCellSnapshot> source_requested =
        sheet.sparse_cells({
            fastxlsx::WorksheetCellReference {2, 1},
            fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::WorksheetCellReference {2, 1},
        });
    check(source_requested.size() == 3 &&
            is_text_snapshot(source_requested[0], 2, 1, "tail") &&
            is_number_snapshot(source_requested[1], 1, 2, 2.0) &&
            is_text_snapshot(source_requested[2], 2, 1, "tail"),
        "inspection source sparse_cells batch should keep requested order and skip missing cells");
    check(sheet.estimated_memory_usage() == source_memory,
        "inspection source reads should keep the materialized memory estimate stable");
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "inspection source reads should not dirty the materialized session");
    check(editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "inspection source reads should not expose dirty materialized diagnostics");

    sheet.set_cell("C3", fastxlsx::CellValue::text("inspection-c3"));
    sheet.set_cell_value("B1", fastxlsx::CellValue::number(5.0));
    sheet.clear_cell_value("A2");
    sheet.set_cell("D2", fastxlsx::CellValue::formula("A1+B1"));
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "inspection mutations should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "inspection mutations should expose final represented sparse cells");
    check(editor.pending_materialized_cell_count() == 5,
        "inspection mutations should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "inspection mutations should expose final sparse bounds");
    const std::size_t dirty_memory = sheet.estimated_memory_usage();
    check(dirty_memory > 0 &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory,
        "inspection mutations should expose the active dirty memory estimate");
    check(sheet.contains_cell("A1") &&
            sheet.contains_cell("B1") &&
            sheet.contains_cell("A2") &&
            sheet.contains_cell("D2") &&
            sheet.contains_cell("C3"),
        "inspection dirty reads should report represented source, blank, formula, and inserted cells");
    check(!sheet.contains_cell("B2") &&
            !sheet.try_cell("B2").has_value(),
        "inspection dirty reads should keep missing B2 absent");
    check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
        "inspection dirty reads should see cleared A2 as an explicit blank");
    check(sheet.get_cell("D2").kind() == fastxlsx::CellValueKind::Formula &&
            sheet.get_cell("D2").text_value() == "A1+B1",
        "inspection dirty reads should see inserted D2 formula");
    check(sheet.estimated_memory_usage() == dirty_memory &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory,
        "inspection dirty reads should keep dirty memory diagnostics stable");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "inspection save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "inspection save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "inspection save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "inspection save_as should write final worksheet dimension");
    check_contains(data_xml, "alpha",
        "inspection save_as should keep source-backed A1");
    check_contains(data_xml, "<v>5",
        "inspection save_as should write updated B1 number");
    check_contains(data_xml, R"(<c r="A2"/>)",
        "inspection save_as should write explicit A2 blank");
    check_contains(data_xml, R"(r="D2")",
        "inspection save_as should write inserted D2 coordinate");
    check_contains(data_xml, "A1+B1",
        "inspection save_as should write inserted D2 formula");
    check_contains(data_xml, R"(r="C3")",
        "inspection save_as should write inserted C3 coordinate");
    check_contains(data_xml, "inspection-c3",
        "inspection save_as should write inserted C3 text");
    check_not_contains(data_xml, "<v>2",
        "inspection save_as should omit overwritten source B1 number");
    check_not_contains(data_xml, "tail",
        "inspection save_as should omit cleared source A2 text");
    check_not_contains(data_xml, R"(r="B2")",
        "inspection save_as should not synthesize missing B2");
    check_inspection_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean inspection no-op save should keep output entries stable");
    check_inspection_output(noop_output);
}

void test_generated_source_catalog_inspection_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path clean_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-catalog-clean-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-catalog-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-catalog-dirty-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.worksheet_names() == std::vector<std::string>({"Data", "Audit"}),
        "catalog inspection should expose planned source worksheet names");
    check(editor.source_worksheet_names() ==
            std::vector<std::string>({"Data", "Audit"}),
        "catalog inspection should expose original source worksheet names");
    check(editor.has_worksheet("Data") &&
            editor.has_worksheet("Audit") &&
            !editor.has_worksheet("Missing"),
        "catalog inspection should answer planned worksheet probes");
    check(editor.has_source_worksheet("Data") &&
            editor.has_source_worksheet("Audit") &&
            !editor.has_source_worksheet("Missing"),
        "catalog inspection should answer source worksheet probes");
    check_clean_catalog_materialized_diagnostics(editor);

    std::optional<fastxlsx::WorksheetEditor> data =
        editor.try_worksheet("Data");
    std::optional<fastxlsx::WorksheetEditor> audit =
        editor.try_worksheet("Audit");
    check(data.has_value() && audit.has_value(),
        "catalog inspection should acquire existing worksheets through try_worksheet");
    check(!editor.try_worksheet("Missing").has_value(),
        "catalog inspection should return nullopt for a missing worksheet");
    check(data->name() == "Data" && audit->name() == "Audit",
        "catalog inspection should keep worksheet handle names");
    check(data->get_cell("A1").text_value() == "alpha" &&
            audit->get_cell("A1").text_value() == "untouched",
        "catalog inspection should read source-backed worksheet handles");
    check(!data->has_pending_changes() &&
            !audit->has_pending_changes() &&
            !editor.has_pending_changes(),
        "catalog inspection should not dirty materialized sessions");
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "catalog inspection should not expose pending materialized diagnostics");
    check_clean_catalog_materialized_diagnostics(editor);

    editor.save_as(clean_output);
    check(!data->has_pending_changes() &&
            !audit->has_pending_changes() &&
            !editor.has_pending_changes(),
        "catalog clean save_as should keep materialized sessions clean");
    check(editor.pending_change_count() == 0,
        "catalog clean save_as should not record a handoff");
    check_clean_catalog_materialized_diagnostics(editor);
    check(fastxlsx::test::read_zip_entries(clean_output) == source_entries,
        "catalog clean save_as should copy the source package entries");
    check_catalog_inspection_output(clean_output, false);

    data->set_cell("C2", fastxlsx::CellValue::text("catalog-dirty"));
    check(data->has_pending_changes() && editor.has_pending_changes(),
        "catalog post-clean edit should dirty the reused materialized handle");
    check(editor.pending_materialized_worksheet_names() ==
            std::vector<std::string>({"Data"}),
        "catalog post-clean edit should expose dirty Data materialized diagnostics");
    check(editor.pending_materialized_cell_count() == 4,
        "catalog post-clean edit should expose dirty sparse cell count");
    const std::size_t dirty_memory = data->estimated_memory_usage();
    check_dirty_catalog_materialized_diagnostics(editor, dirty_memory);

    editor.save_as(dirty_output);
    check(!data->has_pending_changes(),
        "catalog dirty save_as should clean the reused materialized handle");
    check(editor.pending_change_count() == 1,
        "catalog dirty save_as should record one materialized handoff");
    check_clean_catalog_materialized_diagnostics(editor);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "catalog dirty save_as should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(clean_output) == source_entries,
        "catalog dirty save_as should leave the clean no-op output unchanged");

    const auto dirty_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string& data_xml = dirty_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:C2\"",
        "catalog dirty save_as should expand the worksheet dimension");
    check_contains(data_xml, "catalog-dirty",
        "catalog dirty save_as should write the dirty C2 text");
    check_not_contains(data_xml, R"(r="D2")",
        "catalog dirty save_as should not synthesize unrelated cells");
    check_catalog_inspection_output(dirty_output, true);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(dirty_output);
    reopened.save_as(dirty_noop_output);
    check_clean_catalog_materialized_diagnostics(reopened);
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_entries,
        "catalog dirty clean no-op save should keep output entries stable");
    check_catalog_inspection_output(dirty_noop_output, true);
}

void test_generated_source_sparse_initializer_list_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-sparse-init-list-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-sparse-init-list-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    const std::vector<fastxlsx::WorksheetCellSnapshot> source_requested =
        sheet.sparse_cells({
            fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::WorksheetCellReference {1, 2},
        });
    check(source_requested.size() == 3 &&
            is_number_snapshot(source_requested[0], 1, 2, 2.0) &&
            is_text_snapshot(source_requested[1], 1, 1, "alpha") &&
            is_number_snapshot(source_requested[2], 1, 2, 2.0),
        "sparse initializer-list source snapshot should preserve order, duplicates, and missing skips");
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "sparse initializer-list source reads should not dirty the materialized session");

    const bool invalid_mutation_failed = threw_fastxlsx_error([&sheet] {
        sheet.set_cell("a1",
            fastxlsx::CellValue::text("initializer-list-diagnostic-payload"));
    });
    check(invalid_mutation_failed,
        "sparse initializer-list invalid mutation should seed last_edit_error");
    const std::optional<std::string> mutation_error = editor.last_edit_error();
    check(mutation_error.has_value(),
        "sparse initializer-list invalid mutation should expose last_edit_error");
    check(threw_fastxlsx_error([&sheet] {
        (void)sheet.sparse_cells({
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {0, 1},
        });
    }), "sparse initializer-list invalid coordinates should throw as read failures");
    check(editor.last_edit_error() == mutation_error,
        "sparse initializer-list invalid read should preserve prior diagnostics");
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "sparse initializer-list invalid read should leave the session clean");

    sheet.set_cell("C1", fastxlsx::CellValue::number(9.0));
    sheet.clear_cell_value("B1");
    sheet.erase_cell("A2");
    sheet.set_cell("D3", fastxlsx::CellValue::formula("A1+C1"));
    check(!editor.last_edit_error().has_value(),
        "sparse initializer-list valid edits should clear prior diagnostics");
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "sparse initializer-list mutations should dirty the materialized session");
    check(sheet.cell_count() == 4,
        "sparse initializer-list mutations should keep final represented sparse cells");
    check(editor.pending_materialized_cell_count() == 4,
        "sparse initializer-list mutations should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "sparse initializer-list mutations should expose final sparse bounds");

    const std::vector<fastxlsx::WorksheetCellSnapshot> dirty_requested =
        sheet.sparse_cells({
            fastxlsx::WorksheetCellReference {3, 4},
            fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::WorksheetCellReference {2, 1},
            fastxlsx::WorksheetCellReference {3, 4},
            fastxlsx::WorksheetCellReference {1, 3},
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {4, 4},
        });
    check(dirty_requested.size() == 5 &&
            is_formula_snapshot(dirty_requested[0], 3, 4, "A1+C1") &&
            is_blank_snapshot(dirty_requested[1], 1, 2) &&
            is_formula_snapshot(dirty_requested[2], 3, 4, "A1+C1") &&
            is_number_snapshot(dirty_requested[3], 1, 3, 9.0) &&
            is_text_snapshot(dirty_requested[4], 1, 1, "alpha"),
        "sparse initializer-list dirty snapshot should preserve order, duplicates, and missing skips");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "sparse initializer-list save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "sparse initializer-list save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "sparse initializer-list save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "sparse initializer-list save_as should write final worksheet dimension");
    check_contains(data_xml, "alpha",
        "sparse initializer-list save_as should keep source-backed A1");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "sparse initializer-list save_as should write explicit B1 blank");
    check_contains(data_xml, R"(<c r="C1"><v>9</v></c>)",
        "sparse initializer-list save_as should write inserted C1 number");
    check_contains(data_xml, R"(<c r="D3"><f>A1+C1</f></c>)",
        "sparse initializer-list save_as should write inserted D3 formula");
    check_not_contains(data_xml, "tail",
        "sparse initializer-list save_as should omit erased source A2 text");
    check_not_contains(data_xml, "initializer-list-diagnostic-payload",
        "sparse initializer-list save_as should not leak rejected payload");
    check_sparse_initializer_list_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean sparse initializer-list no-op save should keep output entries stable");
    check_sparse_initializer_list_output(noop_output);
}

void test_generated_source_span_batch_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-span-batch-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-span-batch-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);

    const std::array<fastxlsx::WorksheetCellUpdate, 4> full_updates {{
        {fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::CellValue::text("span-full-a")},
        {fastxlsx::WorksheetCellReference {2, 2},
            fastxlsx::CellValue::number(6.0)},
        {fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::CellValue::boolean(true)},
        {fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::CellValue::text("span-full-later-c3")},
    }};
    sheet.set_cells(std::span<const fastxlsx::WorksheetCellUpdate>(
        full_updates.data(), full_updates.size()));
    check(sheet.cell_count() == 5,
        "span full replacements should keep source cells plus inserted span targets");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 3),
        "span full replacements should expand sparse bounds");
    check(sheet.get_cell("A1").text_value() == "span-full-a",
        "span full replacements should overwrite source A1");
    check(sheet.get_cell("B2").number_value() == 6.0,
        "span full replacements should insert B2 number");
    check(sheet.get_cell("C3").text_value() == "span-full-later-c3",
        "span full replacements should apply duplicate later-wins C3");

    const std::array<fastxlsx::WorksheetCellUpdate, 4> value_updates {{
        {fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::CellValue::blank()},
        {fastxlsx::WorksheetCellReference {2, 2},
            fastxlsx::CellValue::text("span-value-first-b2")},
        {fastxlsx::WorksheetCellReference {2, 2},
            fastxlsx::CellValue::formula("A1+B1")},
        {fastxlsx::WorksheetCellReference {3, 4},
            fastxlsx::CellValue::formula("A1+B1")},
    }};
    sheet.set_cell_values(std::span<const fastxlsx::WorksheetCellUpdate>(
        value_updates.data(), value_updates.size()));
    check(sheet.cell_count() == 6,
        "span value writes should preserve and insert represented sparse cells");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "span value writes should convert source B1 to blank");
    check(sheet.get_cell("B2").kind() == fastxlsx::CellValueKind::Formula &&
            sheet.get_cell("B2").text_value() == "A1+B1",
        "span value writes should apply duplicate later-wins B2 formula");
    check(sheet.get_cell("D3").kind() == fastxlsx::CellValueKind::Formula &&
            sheet.get_cell("D3").text_value() == "A1+B1",
        "span value writes should insert D3 formula");

    const std::array<fastxlsx::WorksheetCellReference, 2> clear_targets {{
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {3, 3},
    }};
    sheet.clear_cell_values(std::span<const fastxlsx::WorksheetCellReference>(
        clear_targets.data(), clear_targets.size()));
    check(sheet.cell_count() == 6,
        "span clears should keep represented sparse cell count stable");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "span clears should preserve B1 as explicit blank");
    check(sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Blank,
        "span clears should convert dirty C3 to blank");

    const std::array<fastxlsx::WorksheetCellReference, 3> erase_targets {{
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {2, 2},
        fastxlsx::WorksheetCellReference {5, 5},
    }};
    sheet.erase_cells(std::span<const fastxlsx::WorksheetCellReference>(
        erase_targets.data(), erase_targets.size()));
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "span batch mutations should dirty the materialized session");
    check(sheet.cell_count() == 4,
        "span batch mutations should expose final represented sparse cells");
    check(editor.pending_materialized_cell_count() == 4,
        "span batch mutations should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "span batch mutations should expose final sparse bounds");
    check(!sheet.try_cell("A2").has_value() &&
            !sheet.try_cell("B2").has_value() &&
            !sheet.try_cell("E5").has_value(),
        "span erases should remove present targets and ignore missing targets");
    check(sheet.get_cell("A1").text_value() == "span-full-a",
        "span batch mutations should keep final A1 text");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "span batch mutations should keep final B1 blank");
    check(sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Blank,
        "span batch mutations should keep final C3 blank");
    check(sheet.get_cell("D3").kind() == fastxlsx::CellValueKind::Formula &&
            sheet.get_cell("D3").text_value() == "A1+B1",
        "span batch mutations should keep final D3 formula");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "span batch save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "span batch save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "span batch save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "span batch save_as should write final worksheet dimension");
    check_contains(data_xml, "span-full-a",
        "span batch save_as should write final A1 text");
    check_contains(data_xml, R"(<c r="B1"/>)",
        "span batch save_as should write explicit B1 blank");
    check_contains(data_xml, R"(<c r="C3"/>)",
        "span batch save_as should write cleared C3 blank");
    check_contains(data_xml, R"(<c r="D3"><f>A1+B1</f></c>)",
        "span batch save_as should write final D3 formula");
    check_not_contains(data_xml, "alpha",
        "span batch save_as should omit overwritten source A1 text");
    check_not_contains(data_xml, "tail",
        "span batch save_as should omit erased source A2 text");
    check_not_contains(data_xml, "span-full-later-c3",
        "span batch save_as should omit cleared intermediate C3 text");
    check_not_contains(data_xml, "span-value-first-b2",
        "span batch save_as should omit duplicate intermediate B2 text");
    check_not_contains(data_xml, R"(r="B2")",
        "span batch save_as should omit erased B2 coordinate");
    check_span_batch_output(output);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean span batch no-op save should keep output entries stable");
    check_span_batch_output(noop_output);
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

void test_generated_source_insert_rows_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-insert-rows-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-insert-rows-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C2", fastxlsx::CellValue::text("insert-row-dirty-c2"));
    sheet.set_cell("D1", fastxlsx::CellValue::boolean(false));
    sheet.insert_rows(2, 1);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "insert_rows roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "insert_rows roundtrip should keep represented sparse cell count stable");
    check(editor.pending_materialized_cell_count() == 5,
        "insert_rows roundtrip should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 3, 4),
        "insert_rows roundtrip should expose shifted sparse bounds");
    check(sheet.get_cell("A3").text_value() == "tail",
        "insert_rows roundtrip should shift source-backed A2 to A3");
    check(sheet.get_cell("C3").text_value() == "insert-row-dirty-c2",
        "insert_rows roundtrip should shift dirty C2 to C3");
    check(!sheet.try_cell("A2").has_value() &&
            !sheet.try_cell("C2").has_value(),
        "insert_rows roundtrip should leave old shifted coordinates absent");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "insert_rows roundtrip save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "insert_rows roundtrip save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows roundtrip save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:D3\"",
        "insert_rows roundtrip save_as should write shifted worksheet dimension");
    check_contains(data_xml, R"(<c r="D1" t="b"><v>0</v></c>)",
        "insert_rows roundtrip save_as should keep non-target dirty D1");
    check_contains(data_xml, R"(r="A3")",
        "insert_rows roundtrip save_as should write shifted source A3");
    check_contains(data_xml, R"(r="C3")",
        "insert_rows roundtrip save_as should write shifted dirty C3");
    check_not_contains(data_xml, R"(r="A2")",
        "insert_rows roundtrip save_as should omit old source A2 coordinate");
    check_not_contains(data_xml, R"(r="C2")",
        "insert_rows roundtrip save_as should omit old dirty C2 coordinate");

    const std::vector<ExpectedShiftCell> expected {
        expected_shift_text(1, 1, "alpha"),
        expected_shift_number(1, 2, 2.0),
        expected_shift_boolean(1, 4, false),
        expected_shift_text(3, 1, "tail"),
        expected_shift_text(3, 3, "insert-row-dirty-c2"),
    };
    const std::vector<fastxlsx::WorksheetCellReference> absent {
        {2, 1},
        {2, 3},
    };
    check_structural_shift_output(
        output, fastxlsx::CellRange {1, 1, 3, 4}, expected, absent);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean insert_rows no-op save should keep output entries stable");
    check_structural_shift_output(
        noop_output, fastxlsx::CellRange {1, 1, 3, 4}, expected, absent);
}

void test_generated_source_delete_rows_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-delete-rows-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-delete-rows-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C3", fastxlsx::CellValue::text("delete-row-dirty-c3"));
    sheet.set_cell("D1", fastxlsx::CellValue::text("delete-row-removed-d1"));
    sheet.delete_rows(1, 1);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "delete_rows roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 2,
        "delete_rows roundtrip should remove target-row records and shift later records");
    check(editor.pending_materialized_cell_count() == 2,
        "delete_rows roundtrip should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "delete_rows roundtrip should expose shifted sparse bounds");
    check(sheet.get_cell("A1").text_value() == "tail",
        "delete_rows roundtrip should shift source-backed A2 to A1");
    check(sheet.get_cell("C2").text_value() == "delete-row-dirty-c3",
        "delete_rows roundtrip should shift dirty C3 to C2");
    check(!sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("C3").has_value() &&
            !sheet.try_cell("D1").has_value(),
        "delete_rows roundtrip should omit deleted and old shifted coordinates");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "delete_rows roundtrip save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "delete_rows roundtrip save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows roundtrip save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:C2\"",
        "delete_rows roundtrip save_as should write shifted worksheet dimension");
    check_contains(data_xml, R"(r="A1")",
        "delete_rows roundtrip save_as should write shifted source A1");
    check_contains(data_xml, R"(r="C2")",
        "delete_rows roundtrip save_as should write shifted dirty C2");
    check_not_contains(data_xml, "alpha",
        "delete_rows roundtrip save_as should omit deleted source A1 text");
    check_not_contains(data_xml, "<v>2",
        "delete_rows roundtrip save_as should omit deleted source B1 number");
    check_not_contains(data_xml, "delete-row-removed-d1",
        "delete_rows roundtrip save_as should omit deleted dirty D1 text");
    check_not_contains(data_xml, R"(r="C3")",
        "delete_rows roundtrip save_as should omit old dirty C3 coordinate");

    const std::vector<ExpectedShiftCell> expected {
        expected_shift_text(1, 1, "tail"),
        expected_shift_text(2, 3, "delete-row-dirty-c3"),
    };
    const std::vector<fastxlsx::WorksheetCellReference> absent {
        {1, 2},
        {1, 4},
        {3, 3},
    };
    check_structural_shift_output(
        output, fastxlsx::CellRange {1, 1, 2, 3}, expected, absent);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean delete_rows no-op save should keep output entries stable");
    check_structural_shift_output(
        noop_output, fastxlsx::CellRange {1, 1, 2, 3}, expected, absent);
}

void test_generated_source_insert_columns_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-insert-columns-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-insert-columns-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("B2", fastxlsx::CellValue::text("insert-column-dirty-b2"));
    sheet.set_cell("D1", fastxlsx::CellValue::boolean(true));
    sheet.insert_columns(2, 1);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "insert_columns roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 5,
        "insert_columns roundtrip should keep represented sparse cell count stable");
    check(editor.pending_materialized_cell_count() == 5,
        "insert_columns roundtrip should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 5),
        "insert_columns roundtrip should expose shifted sparse bounds");
    check(sheet.get_cell("C1").number_value() == 2.0,
        "insert_columns roundtrip should shift source-backed B1 to C1");
    check(sheet.get_cell("C2").text_value() == "insert-column-dirty-b2",
        "insert_columns roundtrip should shift dirty B2 to C2");
    check(sheet.get_cell("E1").boolean_value(),
        "insert_columns roundtrip should shift dirty D1 to E1");
    check(!sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("B2").has_value() &&
            !sheet.try_cell("D1").has_value(),
        "insert_columns roundtrip should leave old shifted coordinates absent");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "insert_columns roundtrip save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "insert_columns roundtrip save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns roundtrip save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:E2\"",
        "insert_columns roundtrip save_as should write shifted worksheet dimension");
    check_contains(data_xml, R"(<c r="C1"><v>2</v></c>)",
        "insert_columns roundtrip save_as should write shifted source C1");
    check_contains(data_xml, "insert-column-dirty-b2",
        "insert_columns roundtrip save_as should write shifted dirty C2 text");
    check_contains(data_xml, R"(<c r="E1" t="b"><v>1</v></c>)",
        "insert_columns roundtrip save_as should write shifted dirty E1 boolean");
    check_not_contains(data_xml, R"(r="B1")",
        "insert_columns roundtrip save_as should omit old source B1 coordinate");
    check_not_contains(data_xml, R"(r="B2")",
        "insert_columns roundtrip save_as should omit old dirty B2 coordinate");
    check_not_contains(data_xml, R"(r="D1")",
        "insert_columns roundtrip save_as should omit old dirty D1 coordinate");

    const std::vector<ExpectedShiftCell> expected {
        expected_shift_text(1, 1, "alpha"),
        expected_shift_number(1, 3, 2.0),
        expected_shift_boolean(1, 5, true),
        expected_shift_text(2, 1, "tail"),
        expected_shift_text(2, 3, "insert-column-dirty-b2"),
    };
    const std::vector<fastxlsx::WorksheetCellReference> absent {
        {1, 2},
        {2, 2},
        {1, 4},
    };
    check_structural_shift_output(
        output, fastxlsx::CellRange {1, 1, 2, 5}, expected, absent);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean insert_columns no-op save should keep output entries stable");
    check_structural_shift_output(
        noop_output, fastxlsx::CellRange {1, 1, 2, 5}, expected, absent);
}

void test_generated_source_delete_columns_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-delete-columns-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-delete-columns-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    sheet.set_cell("C2", fastxlsx::CellValue::text("delete-column-dirty-c2"));
    sheet.set_cell("D1", fastxlsx::CellValue::text("delete-column-dirty-d1"));
    sheet.delete_columns(1, 1);
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "delete_columns roundtrip should dirty the materialized session");
    check(sheet.cell_count() == 3,
        "delete_columns roundtrip should remove target-column records and shift later records");
    check(editor.pending_materialized_cell_count() == 3,
        "delete_columns roundtrip should expose final dirty materialized cell count");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 3),
        "delete_columns roundtrip should expose shifted sparse bounds");
    check(sheet.get_cell("A1").number_value() == 2.0,
        "delete_columns roundtrip should shift source-backed B1 to A1");
    check(sheet.get_cell("B2").text_value() == "delete-column-dirty-c2",
        "delete_columns roundtrip should shift dirty C2 to B2");
    check(sheet.get_cell("C1").text_value() == "delete-column-dirty-d1",
        "delete_columns roundtrip should shift dirty D1 to C1");
    check(!sheet.try_cell("A2").has_value() &&
            !sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("C2").has_value() &&
            !sheet.try_cell("D1").has_value(),
        "delete_columns roundtrip should omit deleted and old shifted coordinates");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "delete_columns roundtrip save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "delete_columns roundtrip save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns roundtrip save_as should leave the generated source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_xml, "<dimension ref=\"A1:C2\"",
        "delete_columns roundtrip save_as should write shifted worksheet dimension");
    check_contains(data_xml, R"(<c r="A1"><v>2</v></c>)",
        "delete_columns roundtrip save_as should write shifted source A1");
    check_contains(data_xml, "delete-column-dirty-c2",
        "delete_columns roundtrip save_as should write shifted dirty B2 text");
    check_contains(data_xml, "delete-column-dirty-d1",
        "delete_columns roundtrip save_as should write shifted dirty C1 text");
    check_not_contains(data_xml, "alpha",
        "delete_columns roundtrip save_as should omit deleted source A1 text");
    check_not_contains(data_xml, "tail",
        "delete_columns roundtrip save_as should omit deleted source A2 text");
    check_not_contains(data_xml, R"(r="D1")",
        "delete_columns roundtrip save_as should omit old dirty D1 coordinate");
    check_not_contains(data_xml, R"(r="C2")",
        "delete_columns roundtrip save_as should omit old dirty C2 coordinate");

    const std::vector<ExpectedShiftCell> expected {
        expected_shift_number(1, 1, 2.0),
        expected_shift_text(1, 3, "delete-column-dirty-d1"),
        expected_shift_text(2, 2, "delete-column-dirty-c2"),
    };
    const std::vector<fastxlsx::WorksheetCellReference> absent {
        {2, 1},
        {1, 2},
        {2, 3},
        {1, 4},
    };
    check_structural_shift_output(
        output, fastxlsx::CellRange {1, 1, 2, 3}, expected, absent);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "clean delete_columns no-op save should keep output entries stable");
    check_structural_shift_output(
        noop_output, fastxlsx::CellRange {1, 1, 2, 3}, expected, absent);
}

void test_generated_source_structural_shift_noop_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-shift-noop-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-shift-noop-second-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check_initial_snapshots(sheet);
    check(threw_fastxlsx_error([&sheet] {
        sheet.set_cell("a1",
            fastxlsx::CellValue::text("invalid-shift-noop-payload"));
    }), "shift no-op setup should seed an edit diagnostic");
    check(editor.last_edit_error().has_value(),
        "shift no-op setup should expose the seeded edit diagnostic");
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "shift no-op setup should not dirty the materialized session");

    sheet.insert_rows(2, 0);
    check(!editor.last_edit_error().has_value(),
        "zero-count insert_rows should clear prior edit diagnostics");
    sheet.delete_rows(2, 0);
    sheet.insert_columns(2, 0);
    sheet.delete_columns(2, 0);
    sheet.insert_rows(10, 1);
    sheet.delete_rows(10, 1);
    sheet.insert_columns(10, 1);
    sheet.delete_columns(10, 1);

    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "structural shift no-ops should keep the materialized session clean");
    check(editor.pending_change_count() == 0,
        "structural shift no-ops should not record pending handoffs");
    check(editor.pending_materialized_cell_count() == 0,
        "structural shift no-ops should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "structural shift no-ops should not expose dirty materialized memory");
    check(sheet.cell_count() == 3,
        "structural shift no-ops should preserve source-backed sparse count");
    check(is_used_range(sheet.used_range(), 1, 1, 2, 2),
        "structural shift no-ops should preserve source-backed bounds");
    check(sheet.get_cell("A1").text_value() == "alpha" &&
            sheet.get_cell("B1").number_value() == 2.0 &&
            sheet.get_cell("A2").text_value() == "tail",
        "structural shift no-ops should preserve source-backed cell values");
    check(!sheet.try_cell("B2").has_value() &&
            !sheet.try_cell("J10").has_value(),
        "structural shift no-ops should not synthesize missing sparse cells");

    editor.save_as(output);
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "structural shift no-op save_as should keep the session clean");
    check(editor.pending_change_count() == 0,
        "structural shift no-op save_as should not record materialized handoffs");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "structural shift no-op save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "structural shift no-op save_as should copy original package entries");
    check_initial_snapshots(sheet);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_data = reopened.worksheet("Data");
    check_initial_snapshots(reopened_data);
    fastxlsx::WorksheetEditor reopened_audit = reopened.worksheet("Audit");
    check(reopened_audit.cell_count() == 1 &&
            reopened_audit.get_cell("A1").text_value() == "untouched",
        "reopened structural shift no-op Audit sheet should remain copy-original");

    reopened.save_as(second_output);
    check(fastxlsx::test::read_zip_entries(second_output) == output_entries,
        "clean structural shift no-op save should keep output entries stable");
}

void test_generated_source_empty_literal_noop_roundtrip()
{
    const std::filesystem::path source = write_generated_source_workbook();
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-snapshot-empty-literal-noop-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-empty-literal-noop-second-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-empty-literal-noop-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-snapshot-empty-literal-noop-dirty-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check_initial_snapshots(sheet);

    const auto seed_error = [&editor, &sheet] {
        check(threw_fastxlsx_error([&sheet] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("empty-noop-rejected"));
        }), "empty literal no-op seed mutation should fail");
        check(editor.last_edit_error().has_value(),
            "empty literal no-op seed mutation should expose last_edit_error");
    };
    const auto check_clean_noop_state = [&editor, &sheet] {
        check(!editor.last_edit_error().has_value(),
            "empty literal no-op should clear prior edit diagnostics");
        check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
            "empty literal no-op should keep the materialized session clean");
        check(editor.pending_change_count() == 0,
            "empty literal no-op should not record pending changes");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "empty literal no-op should not expose dirty materialized diagnostics");
        check(editor.pending_worksheet_edits().empty(),
            "empty literal no-op should not expose worksheet edit summaries");
        check(sheet.cell_count() == 3 &&
                is_used_range(sheet.used_range(), 1, 1, 2, 2),
            "empty literal no-op should preserve source sparse shape");
        check(sheet.get_cell("A1").text_value() == "alpha" &&
                sheet.get_cell("B1").number_value() == 2.0 &&
                sheet.get_cell("A2").text_value() == "tail",
            "empty literal no-op should preserve source-backed values");
    };
    const auto run_empty_literal_noops = [&seed_error, &sheet](const auto& check_state) {
        seed_error();
        sheet.append_row(std::initializer_list<fastxlsx::CellValue> {});
        check_state();

        seed_error();
        sheet.set_row(20, std::initializer_list<fastxlsx::CellValue> {});
        check_state();

        seed_error();
        sheet.set_column(20, std::initializer_list<fastxlsx::CellValue> {});
        check_state();

        seed_error();
        sheet.set_cells(std::initializer_list<fastxlsx::WorksheetCellUpdate> {});
        check_state();

        seed_error();
        sheet.set_cell_values(std::initializer_list<fastxlsx::WorksheetCellUpdate> {});
        check_state();

        seed_error();
        sheet.clear_cell_values(
            std::initializer_list<fastxlsx::WorksheetCellReference> {});
        check_state();

        seed_error();
        sheet.erase_cells(std::initializer_list<fastxlsx::WorksheetCellReference> {});
        check_state();
    };

    run_empty_literal_noops(check_clean_noop_state);

    editor.save_as(output);
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "empty literal no-op save_as should keep the session clean");
    check(editor.pending_change_count() == 0,
        "empty literal no-op save_as should not record materialized handoffs");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "empty literal no-op save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "empty literal no-op save_as should copy original package entries");
    check_initial_snapshots(sheet);

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_data = reopened.worksheet("Data");
    check_initial_snapshots(reopened_data);
    fastxlsx::WorksheetEditor reopened_audit = reopened.worksheet("Audit");
    check(reopened_audit.cell_count() == 1 &&
            reopened_audit.get_cell("A1").text_value() == "untouched",
        "reopened empty literal no-op Audit sheet should remain copy-original");

    reopened.save_as(second_output);
    check(fastxlsx::test::read_zip_entries(second_output) == output_entries,
        "clean empty literal no-op save should keep output entries stable");

    sheet.set_cell("C2", fastxlsx::CellValue::text("empty-noop-dirty"));
    const auto check_dirty_noop_state = [&editor, &sheet] {
        check(!editor.last_edit_error().has_value(),
            "dirty empty literal no-op should clear prior edit diagnostics");
        check(sheet.has_pending_changes() && editor.has_pending_changes(),
            "dirty empty literal no-op should keep the materialized session dirty");
        check(editor.pending_materialized_worksheet_names() ==
                std::vector<std::string>({"Data"}) &&
                editor.pending_materialized_cell_count() == 4 &&
                editor.estimated_pending_materialized_memory_usage() ==
                    sheet.estimated_memory_usage(),
            "dirty empty literal no-op should preserve dirty materialized diagnostics");
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1 &&
                summaries[0].source_name == "Data" &&
                summaries[0].planned_name == "Data" &&
                summaries[0].materialized_dirty &&
                summaries[0].materialized_cell_count == 4 &&
                summaries[0].estimated_materialized_memory_usage ==
                    sheet.estimated_memory_usage(),
            "dirty empty literal no-op should preserve the Data materialized summary");
        check(sheet.cell_count() == 4 &&
                is_used_range(sheet.used_range(), 1, 1, 2, 3),
            "dirty empty literal no-op should preserve dirty sparse shape");
        check(sheet.get_cell("A1").text_value() == "alpha" &&
                sheet.get_cell("B1").number_value() == 2.0 &&
                sheet.get_cell("A2").text_value() == "tail" &&
                sheet.get_cell("C2").text_value() == "empty-noop-dirty",
            "dirty empty literal no-op should preserve dirty sparse values");
    };

    run_empty_literal_noops(check_dirty_noop_state);

    editor.save_as(dirty_output);
    check(!sheet.has_pending_changes(),
        "dirty empty literal no-op save_as should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "dirty empty literal no-op save_as should record one materialized handoff");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "dirty empty literal no-op save_as should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "dirty empty literal no-op save_as should leave the clean no-op output unchanged");

    const auto dirty_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string& dirty_data_xml = dirty_entries.at("xl/worksheets/sheet1.xml");
    check_contains(dirty_data_xml, "<dimension ref=\"A1:C2\"",
        "dirty empty literal no-op save_as should expand the worksheet dimension");
    check_contains(dirty_data_xml, "empty-noop-dirty",
        "dirty empty literal no-op save_as should persist the dirty cell");
    check_not_contains(dirty_data_xml, "empty-noop-rejected",
        "dirty empty literal no-op save_as should not leak rejected payloads");

    fastxlsx::WorkbookEditor dirty_reopened = fastxlsx::WorkbookEditor::open(dirty_output);
    fastxlsx::WorksheetEditor dirty_reopened_data =
        dirty_reopened.worksheet("Data");
    check(dirty_reopened_data.cell_count() == 4 &&
            is_used_range(dirty_reopened_data.used_range(), 1, 1, 2, 3) &&
            dirty_reopened_data.get_cell("C2").text_value() == "empty-noop-dirty",
        "reopened dirty empty literal no-op output should expose the dirty cell");
    dirty_reopened.save_as(dirty_noop_output);
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_entries,
        "dirty empty literal no-op clean save should keep output entries stable");
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
        test_generated_source_coordinate_batch_clear_roundtrip();
        test_generated_source_a1_range_clear_roundtrip();
        test_generated_source_coordinate_batch_erase_roundtrip();
        test_generated_source_a1_range_erase_roundtrip();
        test_generated_source_row_column_clear_roundtrip();
        test_generated_source_row_column_range_clear_roundtrip();
        test_generated_source_row_column_erase_roundtrip();
        test_generated_source_row_column_range_erase_roundtrip();
        test_generated_source_append_row_roundtrip();
        test_generated_source_sparse_batch_replacement_roundtrip();
        test_generated_source_single_value_roundtrip();
        test_generated_source_contains_cell_roundtrip();
        test_generated_source_inspection_roundtrip();
        test_generated_source_catalog_inspection_roundtrip();
        test_generated_source_sparse_initializer_list_roundtrip();
        test_generated_source_span_batch_roundtrip();
        test_generated_source_sparse_value_batch_roundtrip();
        test_generated_source_row_column_replacement_roundtrip();
        test_generated_source_row_column_value_roundtrip();
        test_generated_source_insert_rows_roundtrip();
        test_generated_source_delete_rows_roundtrip();
        test_generated_source_insert_columns_roundtrip();
        test_generated_source_delete_columns_roundtrip();
        test_generated_source_structural_shift_noop_roundtrip();
        test_generated_source_empty_literal_noop_roundtrip();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
