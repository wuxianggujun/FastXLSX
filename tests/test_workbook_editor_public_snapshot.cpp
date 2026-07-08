#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include "zip_test_utils.hpp"

#include <exception>
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

void check_initial_snapshots(fastxlsx::WorksheetEditor& sheet)
{
    check(sheet.cell_count() == 3,
        "generated source snapshot should materialize three cells");

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
    check(data.get_cell("C2").text_value() == "snapshot-edit",
        "reopened Data snapshot output should read the saved edit");

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

    check(threw_fastxlsx_error([&sheet] { (void)sheet.row_cells(0); }),
        "row_cells should reject invalid row coordinates");
    check(editor.last_edit_error() == mutation_error,
        "row_cells invalid read should preserve prior last_edit_error");
    check(threw_fastxlsx_error([&sheet] { (void)sheet.column_cells(0); }),
        "column_cells should reject invalid column coordinates");
    check(editor.last_edit_error() == mutation_error,
        "column_cells invalid read should preserve prior last_edit_error");
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

} // namespace

int main()
{
    try {
        test_generated_source_snapshot_edit_roundtrip();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
