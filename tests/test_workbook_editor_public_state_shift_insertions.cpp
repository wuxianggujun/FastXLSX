#include "test_workbook_editor_public_state_shifts_support.hpp"

namespace {

void test_public_worksheet_editor_shift_snapshots_are_owning_across_later_shifts()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-shift-owning-snapshots-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-shift-owning-snapshots-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-shift-owning-snapshots-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::vector<fastxlsx::WorksheetCellSnapshot> full_snapshot =
        sheet.sparse_cells();
    const std::vector<fastxlsx::WorksheetCellSnapshot> range_snapshot =
        sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    const std::vector<fastxlsx::WorksheetCellSnapshot> a1_range_snapshot =
        sheet.sparse_cells("A1:B2");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_snapshot =
        sheet.row_cells(1);
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_snapshot =
        sheet.column_cells(1);
    const std::array<fastxlsx::WorksheetCellReference, 4> requested_cells {
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {1, 1},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> batch_snapshot =
        sheet.sparse_cells(requested_cells);

    const auto check_original_sparse_snapshot =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cells.size() == 3,
                prefix + " should keep the original source sparse count");
            if (cells.size() == 3) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "placeholder-a1",
                    prefix + " should keep original A1 text");
                check(cells[1].reference.row == 1 &&
                        cells[1].reference.column == 2 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[1].value.number_value() == 1.0,
                    prefix + " should keep original B1 number");
                check(cells[2].reference.row == 2 &&
                        cells[2].reference.column == 1 &&
                        cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[2].value.text_value() == "placeholder-a2",
                    prefix + " should keep original A2 text");
            }
        };
    const auto check_original_row_snapshot =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cells.size() == 2,
                prefix + " should keep original row-one count");
            if (cells.size() == 2) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "placeholder-a1",
                    prefix + " should keep original row-one A1 text");
                check(cells[1].reference.row == 1 &&
                        cells[1].reference.column == 2 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[1].value.number_value() == 1.0,
                    prefix + " should keep original row-one B1 number");
            }
        };
    const auto check_original_column_snapshot =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cells.size() == 2,
                prefix + " should keep original column-one count");
            if (cells.size() == 2) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "placeholder-a1",
                    prefix + " should keep original column-one A1 text");
                check(cells[1].reference.row == 2 &&
                        cells[1].reference.column == 1 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[1].value.text_value() == "placeholder-a2",
                    prefix + " should keep original column-one A2 text");
            }
        };
    const auto check_original_batch_snapshot =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cells.size() == 4,
                prefix + " should preserve requested duplicates");
            if (cells.size() == 4) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.text_value() == "placeholder-a1",
                    prefix + " should keep first requested A1 text");
                check(cells[1].reference.row == 2 &&
                        cells[1].reference.column == 1 &&
                        cells[1].value.text_value() == "placeholder-a2",
                    prefix + " should keep requested A2 text");
                check(cells[2].reference.row == 1 &&
                        cells[2].reference.column == 2 &&
                        cells[2].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[2].value.number_value() == 1.0,
                    prefix + " should keep requested B1 number");
                check(cells[3].reference.row == 1 &&
                        cells[3].reference.column == 1 &&
                        cells[3].value.text_value() == "placeholder-a1",
                    prefix + " should keep duplicate requested A1 text");
            }
        };

    sheet.insert_rows(1, 1);
    sheet.insert_columns(1, 1);

    check_original_sparse_snapshot(full_snapshot,
        "shift owning full snapshot");
    check_original_sparse_snapshot(range_snapshot,
        "shift owning CellRange snapshot");
    check_original_sparse_snapshot(a1_range_snapshot,
        "shift owning A1 range snapshot");
    check_original_row_snapshot(row_snapshot,
        "shift owning row snapshot");
    check_original_column_snapshot(column_snapshot,
        "shift owning column snapshot");
    check_original_batch_snapshot(batch_snapshot,
        "shift owning coordinate-batch snapshot");

    const auto inspect_shifted_state =
        [](fastxlsx::WorksheetEditor& inspected_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(inspected_sheet.cell_count() == 3,
                prefix + " should keep shifted sparse count stable");
            check_cell_range_equals(inspected_sheet.used_range(), 2, 2, 3, 3,
                prefix + " should expose shifted sparse bounds");
            check(!inspected_sheet.try_cell("A1").has_value() &&
                    !inspected_sheet.try_cell("B1").has_value() &&
                    !inspected_sheet.try_cell("A2").has_value(),
                prefix + " should keep old coordinates absent");
            const fastxlsx::CellValue shifted_b2 = inspected_sheet.get_cell("B2");
            check(shifted_b2.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_b2.text_value() == "placeholder-a1",
                prefix + " should expose shifted A1 text at B2");
            const fastxlsx::CellValue shifted_c2 = inspected_sheet.get_cell("C2");
            check(shifted_c2.kind() == fastxlsx::CellValueKind::Number &&
                    shifted_c2.number_value() == 1.0,
                prefix + " should expose shifted B1 number at C2");
            const fastxlsx::CellValue shifted_b3 = inspected_sheet.get_cell("B3");
            check(shifted_b3.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_b3.text_value() == "placeholder-a2",
                prefix + " should expose shifted A2 text at B3");
            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row =
                inspected_sheet.row_cells(2);
            check(shifted_row.size() == 2 &&
                    shifted_row[0].reference.row == 2 &&
                    shifted_row[0].reference.column == 2 &&
                    shifted_row[0].value.text_value() == "placeholder-a1" &&
                    shifted_row[1].reference.row == 2 &&
                    shifted_row[1].reference.column == 3 &&
                    shifted_row[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    shifted_row[1].value.number_value() == 1.0,
                prefix + " row_cells should expose shifted row-two cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column =
                inspected_sheet.column_cells(2);
            check(shifted_column.size() == 2 &&
                    shifted_column[0].reference.row == 2 &&
                    shifted_column[0].reference.column == 2 &&
                    shifted_column[0].value.text_value() == "placeholder-a1" &&
                    shifted_column[1].reference.row == 3 &&
                    shifted_column[1].reference.column == 2 &&
                    shifted_column[1].value.text_value() == "placeholder-a2",
                prefix + " column_cells should expose shifted column-two cells");
        };

    inspect_shifted_state(sheet, "shift owning live state");
    check(sheet.has_pending_changes(),
        "shift owning later shifts should dirty the materialized sheet");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 3 &&
            editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "shift owning later shifts should expose dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift owning later shifts should keep diagnostics clear");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "shift owning save should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "shift owning save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift owning save should clear dirty materialized diagnostics");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift owning save should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="B2:C3"/>)",
        "shift owning save should project shifted sparse bounds");
    check_contains(worksheet_xml, R"(<c r="B2" t="inlineStr">)",
        "shift owning save should write shifted A1 text at B2");
    check_contains(worksheet_xml, R"(<c r="C2"><v>1</v></c>)",
        "shift owning save should write shifted B1 number at C2");
    check_contains(worksheet_xml, R"(<c r="B3" t="inlineStr">)",
        "shift owning save should write shifted A2 text at B3");
    check_not_contains(worksheet_xml, R"(r="A1")",
        "shift owning save should omit old A1 coordinate");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "shift owning save should omit old B1 coordinate");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "shift owning save should omit old A2 coordinate");
    check_reopened_shift_output(output, "shift owning snapshots",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_shifted_state(reopened_sheet, "shift owning reopened output");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "shift owning no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "shift owning no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift owning no-op save should keep dirty diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "shift owning no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "shift owning no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "shift owning no-op output should match shifted output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift owning no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "shift owning snapshots no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_shifted_state(
                reopened_sheet, "shift owning reopened no-op output");
        });
}

void test_public_worksheet_editor_delete_snapshots_are_owning_across_later_shifts()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-delete-owning-snapshots-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-delete-owning-snapshots-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-delete-owning-snapshots-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(3, 1, fastxlsx::CellValue::text("delete-snapshot-a3"));
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("delete-snapshot-c3"));
    sheet.set_cell(1, 4, fastxlsx::CellValue::text("delete-snapshot-d1"));

    const std::vector<fastxlsx::WorksheetCellSnapshot> full_snapshot =
        sheet.sparse_cells();
    const std::vector<fastxlsx::WorksheetCellSnapshot> range_snapshot =
        sheet.sparse_cells(fastxlsx::CellRange {1, 1, 3, 4});
    const std::vector<fastxlsx::WorksheetCellSnapshot> a1_range_snapshot =
        sheet.sparse_cells("A1:D3");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_snapshot =
        sheet.row_cells(3);
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_snapshot =
        sheet.column_cells(1);
    const std::array<fastxlsx::WorksheetCellReference, 4> requested_cells {
        fastxlsx::WorksheetCellReference {3, 3},
        fastxlsx::WorksheetCellReference {1, 4},
        fastxlsx::WorksheetCellReference {3, 1},
        fastxlsx::WorksheetCellReference {3, 3},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> batch_snapshot =
        sheet.sparse_cells(requested_cells);

    const auto check_text_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& cell,
            std::uint32_t row,
            std::uint32_t column,
            std::string_view text,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cell.reference.row == row && cell.reference.column == column &&
                    cell.value.kind() == fastxlsx::CellValueKind::Text &&
                    cell.value.text_value() == text,
                prefix + " should keep the expected text snapshot");
        };
    const auto check_number_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& cell,
            std::uint32_t row,
            std::uint32_t column,
            double number,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cell.reference.row == row && cell.reference.column == column &&
                    cell.value.kind() == fastxlsx::CellValueKind::Number &&
                    cell.value.number_value() == number,
                prefix + " should keep the expected number snapshot");
        };
    const auto check_predelete_sparse_snapshot =
        [&](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cells.size() == 6,
                prefix + " should keep the original mixed sparse count");
            if (cells.size() == 6) {
                check_text_snapshot(cells[0], 1, 1, "placeholder-a1", prefix + " A1");
                check_number_snapshot(cells[1], 1, 2, 1.0, prefix + " B1");
                check_text_snapshot(cells[2], 1, 4, "delete-snapshot-d1", prefix + " D1");
                check_text_snapshot(cells[3], 2, 1, "placeholder-a2", prefix + " A2");
                check_text_snapshot(cells[4], 3, 1, "delete-snapshot-a3", prefix + " A3");
                check_text_snapshot(cells[5], 3, 3, "delete-snapshot-c3", prefix + " C3");
            }
        };
    const auto check_predelete_row_snapshot =
        [&](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cells.size() == 2,
                prefix + " should keep original row-three count");
            if (cells.size() == 2) {
                check_text_snapshot(cells[0], 3, 1, "delete-snapshot-a3", prefix + " A3");
                check_text_snapshot(cells[1], 3, 3, "delete-snapshot-c3", prefix + " C3");
            }
        };
    const auto check_predelete_column_snapshot =
        [&](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cells.size() == 3,
                prefix + " should keep original column-one count");
            if (cells.size() == 3) {
                check_text_snapshot(cells[0], 1, 1, "placeholder-a1", prefix + " A1");
                check_text_snapshot(cells[1], 2, 1, "placeholder-a2", prefix + " A2");
                check_text_snapshot(cells[2], 3, 1, "delete-snapshot-a3", prefix + " A3");
            }
        };
    const auto check_predelete_batch_snapshot =
        [&](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(cells.size() == 4,
                prefix + " should preserve requested duplicates");
            if (cells.size() == 4) {
                check_text_snapshot(cells[0], 3, 3, "delete-snapshot-c3", prefix + " first C3");
                check_text_snapshot(cells[1], 1, 4, "delete-snapshot-d1", prefix + " D1");
                check_text_snapshot(cells[2], 3, 1, "delete-snapshot-a3", prefix + " A3");
                check_text_snapshot(cells[3], 3, 3, "delete-snapshot-c3", prefix + " duplicate C3");
            }
        };

    sheet.delete_rows(2, 1);
    sheet.delete_columns(2, 1);

    check_predelete_sparse_snapshot(full_snapshot,
        "delete owning full snapshot");
    check_predelete_sparse_snapshot(range_snapshot,
        "delete owning CellRange snapshot");
    check_predelete_sparse_snapshot(a1_range_snapshot,
        "delete owning A1 range snapshot");
    check_predelete_row_snapshot(row_snapshot,
        "delete owning row snapshot");
    check_predelete_column_snapshot(column_snapshot,
        "delete owning column snapshot");
    check_predelete_batch_snapshot(batch_snapshot,
        "delete owning coordinate-batch snapshot");

    const auto inspect_deleted_state =
        [](fastxlsx::WorksheetEditor& inspected_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(inspected_sheet.cell_count() == 4,
                prefix + " should expose the surviving shifted sparse count");
            check_cell_range_equals(inspected_sheet.used_range(), 1, 1, 2, 3,
                prefix + " should expose surviving sparse bounds");
            check(!inspected_sheet.try_cell("B1").has_value() &&
                    !inspected_sheet.try_cell("A3").has_value() &&
                    !inspected_sheet.try_cell("C3").has_value() &&
                    !inspected_sheet.try_cell("D1").has_value(),
                prefix + " should keep deleted or old coordinates absent");

            const fastxlsx::CellValue a1 = inspected_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Text &&
                    a1.text_value() == "placeholder-a1",
                prefix + " should keep surviving source A1 text");
            const fastxlsx::CellValue c1 = inspected_sheet.get_cell("C1");
            check(c1.kind() == fastxlsx::CellValueKind::Text &&
                    c1.text_value() == "delete-snapshot-d1",
                prefix + " should move D1 text to C1");
            const fastxlsx::CellValue a2 = inspected_sheet.get_cell("A2");
            check(a2.kind() == fastxlsx::CellValueKind::Text &&
                    a2.text_value() == "delete-snapshot-a3",
                prefix + " should move A3 text to A2");
            const fastxlsx::CellValue b2 = inspected_sheet.get_cell("B2");
            check(b2.kind() == fastxlsx::CellValueKind::Text &&
                    b2.text_value() == "delete-snapshot-c3",
                prefix + " should move C3 text to B2");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                inspected_sheet.row_cells(1);
            check(row_one.size() == 2 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.text_value() == "placeholder-a1" &&
                    row_one[1].reference.row == 1 &&
                    row_one[1].reference.column == 3 &&
                    row_one[1].value.text_value() == "delete-snapshot-d1",
                prefix + " row_cells should expose surviving row-one cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                inspected_sheet.row_cells(2);
            check(row_two.size() == 2 &&
                    row_two[0].reference.row == 2 &&
                    row_two[0].reference.column == 1 &&
                    row_two[0].value.text_value() == "delete-snapshot-a3" &&
                    row_two[1].reference.row == 2 &&
                    row_two[1].reference.column == 2 &&
                    row_two[1].value.text_value() == "delete-snapshot-c3",
                prefix + " row_cells should expose surviving row-two cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                inspected_sheet.column_cells(1);
            check(column_one.size() == 2 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.text_value() == "placeholder-a1" &&
                    column_one[1].reference.row == 2 &&
                    column_one[1].reference.column == 1 &&
                    column_one[1].value.text_value() == "delete-snapshot-a3",
                prefix + " column_cells should expose surviving column-one cells");
        };

    inspect_deleted_state(sheet, "delete owning live state");
    check(sheet.has_pending_changes(),
        "delete owning later shifts should dirty the materialized sheet");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "delete owning later shifts should expose dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete owning later shifts should keep diagnostics clear");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "delete owning save should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "delete owning save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete owning save should clear dirty materialized diagnostics");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete owning save should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "delete owning save should project surviving sparse bounds");
    check_contains(worksheet_xml, R"(<c r="A1" t="inlineStr">)",
        "delete owning save should write surviving source A1 text");
    check_contains(worksheet_xml, "placeholder-a1",
        "delete owning save should preserve A1 source text");
    check_contains(worksheet_xml, R"(<c r="C1" t="inlineStr">)",
        "delete owning save should write shifted D1 text at C1");
    check_contains(worksheet_xml, "delete-snapshot-d1",
        "delete owning save should preserve shifted D1 text");
    check_contains(worksheet_xml, R"(<c r="A2" t="inlineStr">)",
        "delete owning save should write shifted A3 text at A2");
    check_contains(worksheet_xml, "delete-snapshot-a3",
        "delete owning save should preserve shifted A3 text");
    check_contains(worksheet_xml, R"(<c r="B2" t="inlineStr">)",
        "delete owning save should write shifted C3 text at B2");
    check_contains(worksheet_xml, "delete-snapshot-c3",
        "delete owning save should preserve shifted C3 text");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "delete owning save should omit deleted source B1 coordinate");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "delete owning save should omit deleted source A2 text");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "delete owning save should omit old A3 coordinate");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "delete owning save should omit old C3 coordinate");
    check_not_contains(worksheet_xml, R"(r="D1")",
        "delete owning save should omit old D1 coordinate");
    check_reopened_shift_output(output, "delete owning snapshots",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_deleted_state(reopened_sheet, "delete owning reopened output");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "delete owning no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "delete owning no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete owning no-op save should keep dirty diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "delete owning no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "delete owning no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "delete owning no-op output should match delete-shifted output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete owning no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "delete owning snapshots no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_deleted_state(
                reopened_sheet, "delete owning reopened no-op output");
        });
}

void check_shift_reacquire_retry_snapshots(
    fastxlsx::WorksheetEditor& snapshot_sheet,
    std::string_view scenario)
{
    const std::string label(scenario);
    const std::size_t baseline_cell_count = snapshot_sheet.cell_count();
    const std::size_t baseline_memory = snapshot_sheet.estimated_memory_usage();
    check(baseline_cell_count == 3,
        label + " cell_count should expose the combined shifted sparse count");
    check_cell_range_equals(snapshot_sheet.used_range(), 1, 1, 3, 3,
        label + " used_range should expose the combined shifted bounds");
    check(snapshot_sheet.contains_cell("A1") &&
            snapshot_sheet.contains_cell("C1") &&
            snapshot_sheet.contains_cell("A3"),
        label + " contains_cell should find shifted scalar coordinates");
    check(!snapshot_sheet.contains_cell("B1") &&
            !snapshot_sheet.contains_cell("A2") &&
            !snapshot_sheet.contains_cell("B2"),
        label + " contains_cell should keep old shifted coordinates absent");
    const std::optional<fastxlsx::CellValue> shifted_number =
        snapshot_sheet.try_cell("C1");
    check(shifted_number.has_value() &&
            shifted_number->kind() == fastxlsx::CellValueKind::Number &&
            shifted_number->number_value() == 1.0,
        label + " try_cell should expose shifted C1 number");
    const fastxlsx::CellValue shifted_text = snapshot_sheet.get_cell("A3");
    check(shifted_text.kind() == fastxlsx::CellValueKind::Text &&
            shifted_text.text_value() == "placeholder-a2",
        label + " get_cell should expose shifted A3 text");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        snapshot_sheet.sparse_cells();
    check(all_cells.size() == 3,
        label + " sparse_cells should expose the combined shifted sparse count");
    if (all_cells.size() == 3) {
        check(all_cells[0].reference.row == 1 && all_cells[0].reference.column == 1 &&
                all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                all_cells[0].value.text_value() == "placeholder-a1",
            label + " sparse_cells should keep A1 first");
        check(all_cells[1].reference.row == 1 && all_cells[1].reference.column == 3 &&
                all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                all_cells[1].value.number_value() == 1.0,
            label + " sparse_cells should expose shifted B1 as C1");
        check(all_cells[2].reference.row == 3 && all_cells[2].reference.column == 1 &&
                all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                all_cells[2].value.text_value() == "placeholder-a2",
            label + " sparse_cells should expose shifted A2 as A3");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
        snapshot_sheet.sparse_cells("A1:C3");
    check(shifted_range.size() == 3,
        label + " range sparse_cells should expose all combined shifted cells");
    if (shifted_range.size() == 3) {
        check(shifted_range[0].reference.row == 1 &&
                shifted_range[0].reference.column == 1 &&
                shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                shifted_range[0].value.text_value() == "placeholder-a1",
            label + " range sparse_cells should keep A1 first");
        check(shifted_range[1].reference.row == 1 &&
                shifted_range[1].reference.column == 3 &&
                shifted_range[1].value.kind() == fastxlsx::CellValueKind::Number &&
                shifted_range[1].value.number_value() == 1.0,
            label + " range sparse_cells should keep shifted C1 second");
        check(shifted_range[2].reference.row == 3 &&
                shifted_range[2].reference.column == 1 &&
                shifted_range[2].value.kind() == fastxlsx::CellValueKind::Text &&
                shifted_range[2].value.text_value() == "placeholder-a2",
            label + " range sparse_cells should keep shifted A3 third");
    }

    const std::array<fastxlsx::WorksheetCellReference, 6> requested_refs {
        fastxlsx::WorksheetCellReference {1, 3},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {3, 1},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {1, 3},
        fastxlsx::WorksheetCellReference {3, 1},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
        snapshot_sheet.sparse_cells(requested_refs);
    check(requested_cells.size() == 4,
        label + " requested sparse_cells should skip old shifted coordinates and keep duplicates");
    if (requested_cells.size() == 4) {
        check(requested_cells[0].reference.row == 1 &&
                requested_cells[0].reference.column == 3 &&
                requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                requested_cells[0].value.number_value() == 1.0,
            label + " requested sparse_cells should keep shifted C1 input order");
        check(requested_cells[1].reference.row == 3 &&
                requested_cells[1].reference.column == 1 &&
                requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[1].value.text_value() == "placeholder-a2",
            label + " requested sparse_cells should keep shifted A3 after skipped cells");
        check(requested_cells[2].reference.row == 1 &&
                requested_cells[2].reference.column == 3 &&
                requested_cells[2].value.kind() == fastxlsx::CellValueKind::Number &&
                requested_cells[2].value.number_value() == 1.0,
            label + " requested sparse_cells should preserve duplicate shifted C1");
        check(requested_cells[3].reference.row == 3 &&
                requested_cells[3].reference.column == 1 &&
                requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[3].value.text_value() == "placeholder-a2",
            label + " requested sparse_cells should preserve duplicate shifted A3");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        snapshot_sheet.row_cells(1);
    check(row_one.size() == 2,
        label + " row_cells should expose shifted row-one cells");
    if (row_one.size() == 2) {
        check(row_one[0].reference.row == 1 &&
                row_one[0].reference.column == 1 &&
                row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                row_one[0].value.text_value() == "placeholder-a1",
            label + " row_cells should keep A1 first");
        check(row_one[1].reference.row == 1 &&
                row_one[1].reference.column == 3 &&
                row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                row_one[1].value.number_value() == 1.0,
            label + " row_cells should keep shifted C1 second");
    }
    check(snapshot_sheet.row_cells(2).empty(),
        label + " row_cells should keep the inserted row gap empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
        snapshot_sheet.row_cells(3);
    check(row_three.size() == 1,
        label + " row_cells should expose the shifted row-three source cell");
    if (row_three.size() == 1) {
        check(row_three[0].reference.row == 3 &&
                row_three[0].reference.column == 1 &&
                row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                row_three[0].value.text_value() == "placeholder-a2",
            label + " row_cells should keep shifted A3 in row three");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        snapshot_sheet.column_cells(1);
    check(column_one.size() == 2,
        label + " column_cells should expose source and row-shifted cells");
    if (column_one.size() == 2) {
        check(column_one[0].reference.row == 1 &&
                column_one[0].reference.column == 1 &&
                column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                column_one[0].value.text_value() == "placeholder-a1",
            label + " column_cells should keep A1 first");
        check(column_one[1].reference.row == 3 &&
                column_one[1].reference.column == 1 &&
                column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                column_one[1].value.text_value() == "placeholder-a2",
            label + " column_cells should keep shifted A3 second");
    }
    check(snapshot_sheet.column_cells(2).empty(),
        label + " column_cells should keep the inserted column gap empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        snapshot_sheet.column_cells(3);
    check(column_three.size() == 1,
        label + " column_cells should expose the shifted numeric column");
    if (column_three.size() == 1) {
        check(column_three[0].reference.row == 1 &&
                column_three[0].reference.column == 3 &&
                column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                column_three[0].value.number_value() == 1.0,
            label + " column_cells should keep shifted C1 in column three");
    }
    check(!snapshot_sheet.has_pending_changes(),
        label + " read-only scalar and snapshot observers should keep the sheet clean");
    check(snapshot_sheet.cell_count() == baseline_cell_count,
        label + " read-only scalar and snapshot observers should preserve cell_count");
    check(snapshot_sheet.estimated_memory_usage() == baseline_memory,
        label + " read-only scalar and snapshot observers should preserve memory estimate");
    check_cell_range_equals(snapshot_sheet.used_range(), 1, 1, 3, 3,
        label + " read-only scalar and snapshot observers should preserve used_range");
}


void test_public_worksheet_editor_insert_rows_shifts_sparse_records()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-insert-rows-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-rows-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-rows-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-rows-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-rows-post-noop-output.xlsx");
    const std::filesystem::path post_noop_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-post-noop-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(3, 3, fastxlsx::CellValue::text("extra-c3"));
    sheet.insert_rows(2, 2);

    check(sheet.cell_count() == 8,
        "insert_rows should preserve sparse cell count when it only shifts records");
    const std::optional<fastxlsx::CellValue> shifted_a1 = sheet.try_cell("A1");
    check(shifted_a1.has_value() && shifted_a1->text_value() == "placeholder-a1",
        "insert_rows should preserve cells above the insertion point");
    const std::optional<fastxlsx::CellValue> shifted_b1 = sheet.try_cell("B1");
    check(shifted_b1.has_value() && shifted_b1->number_value() == 1.0,
        "insert_rows should preserve same-row cells above the insertion point");
    const std::optional<fastxlsx::CellValue> shifted_a4 = sheet.try_cell("A4");
    check(shifted_a4.has_value() && shifted_a4->text_value() == "placeholder-a2",
        "insert_rows should shift source-backed cells downward by row_count");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D4");
    check(shifted_formula.has_value()
            && shifted_formula->kind() == fastxlsx::CellValueKind::Formula
            && shifted_formula->text_value() == "A1+B1"
            && shifted_formula->has_style()
            && shifted_formula->style_id().value() == styled_formula_style.value(),
        "insert_rows should translate moved formula text and preserve the source style id");
    const std::optional<fastxlsx::CellValue> shifted_filler = sheet.try_cell("B4");
    check(shifted_filler.has_value() && shifted_filler->text_value() == "row2-gap-b2",
        "insert_rows should preserve the shifted second-row filler cell");
    const std::optional<fastxlsx::CellValue> shifted_a5 = sheet.try_cell("A5");
    check(shifted_a5.has_value() && shifted_a5->text_value() == "extra-c3",
        "insert_rows should shift source-backed trailing cells downward by row_count");
    const std::optional<fastxlsx::CellValue> shifted_c5 = sheet.try_cell("C5");
    check(shifted_c5.has_value() && shifted_c5->text_value() == "extra-c3",
        "insert_rows should shift dirty cells downward by row_count");
    check(!sheet.try_cell("A2").has_value(),
        "insert_rows should leave the inserted sparse row without synthesized cells");
    check(!sheet.try_cell("D2").has_value(),
        "insert_rows should remove the old shifted formula coordinate");
    check(!sheet.try_cell("C3").has_value(),
        "insert_rows should remove the old shifted sparse coordinate");
    check_cell_range_equals(sheet.used_range(), 1, 1, 5, 4,
        "insert_rows should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_four = sheet.row_cells(4);
    check(shifted_row_four.size() == 4,
        "insert_rows row_cells should expose the shifted row snapshot");
    check(shifted_row_four[0].reference.row == 4 && shifted_row_four[0].reference.column == 1 &&
            shifted_row_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_four[0].value.text_value() == "placeholder-a2",
        "insert_rows row_cells should keep the shifted source-backed cell first");
    check(shifted_row_four[1].reference.row == 4 && shifted_row_four[1].reference.column == 2 &&
            shifted_row_four[1].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_four[1].value.text_value() == "row2-gap-b2",
        "insert_rows row_cells should keep the shifted filler cell in column order");
    check(shifted_row_four[2].reference.row == 4 && shifted_row_four[2].reference.column == 3 &&
            shifted_row_four[2].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_four[2].value.text_value() == "row2-gap-c2",
        "insert_rows row_cells should keep the second shifted filler cell in column order");
    check(shifted_row_four[3].reference.row == 4 && shifted_row_four[3].reference.column == 4 &&
            shifted_row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_four[3].value.text_value() == "A1+B1" &&
            shifted_row_four[3].value.has_style() &&
            shifted_row_four[3].value.style_id().value() == styled_formula_style.value(),
        "insert_rows row_cells should keep the translated formula cell and style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_four =
        sheet.column_cells(4);
    check(shifted_column_four.size() == 1,
        "insert_rows column_cells should expose the shifted formula column snapshot");
    check(shifted_column_four[0].reference.row == 4 &&
            shifted_column_four[0].reference.column == 4 &&
            shifted_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_four[0].value.text_value() == "A1+B1" &&
            shifted_column_four[0].value.has_style() &&
            shifted_column_four[0].value.style_id().value() == styled_formula_style.value(),
        "insert_rows column_cells should keep the translated formula cell and style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "insert_rows pre-save shift summary");
    check(sheet.has_pending_changes(),
        "insert_rows should dirty the materialized worksheet when records shift");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "insert_rows should report the dirty materialized worksheet name");
    check(editor.pending_materialized_cell_count() == 8,
        "insert_rows should keep aggregate materialized cell count stable");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "insert_rows should keep aggregate materialized memory stable");
    check(!editor.last_edit_error().has_value(),
        "successful insert_rows should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "insert_rows save_as should project the shifted sparse dimension");
    check_contains(worksheet_xml, styled_formula_xml,
        "insert_rows save_as should write the translated formula cell with the preserved style id");
    check_contains(worksheet_xml, R"(<c r="A4")",
        "insert_rows save_as should write the shifted source-backed row coordinate");
    check_contains(worksheet_xml, R"(<c r="B4")",
        "insert_rows save_as should write the shifted filler cell");
    check_contains(worksheet_xml, R"(<c r="A5")",
        "insert_rows save_as should write the shifted source-backed trailing row coordinate");
    check_contains(worksheet_xml, R"(<c r="C5")",
        "insert_rows save_as should write the shifted dirty row coordinate");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "insert_rows save_as should not keep the old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "insert_rows save_as should not keep the old source-backed row coordinate");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "insert_rows save_as should not keep the old dirty row coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "insert_rows should preserve untouched worksheets");
    const auto inspect_insert_rows_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "insert_rows reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
                "insert_rows reopened output should expose shifted used range");
            const std::optional<fastxlsx::CellValue> reopened_a1 =
                reopened_sheet.try_cell("A1");
            check(reopened_a1.has_value() &&
                    reopened_a1->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1->text_value() == "placeholder-a1",
                "insert_rows reopened output should keep A1 above the insertion point");
            const std::optional<fastxlsx::CellValue> reopened_b1 =
                reopened_sheet.try_cell("B1");
            check(reopened_b1.has_value() &&
                    reopened_b1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1->number_value() == 1.0,
                "insert_rows reopened output should keep B1 above the insertion point");
            const std::optional<fastxlsx::CellValue> reopened_a4 =
                reopened_sheet.try_cell("A4");
            check(reopened_a4.has_value() &&
                    reopened_a4->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a4->text_value() == "placeholder-a2",
                "insert_rows reopened output should read shifted source A2 at A4");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d4->text_value() == "A1+B1" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "insert_rows reopened output should read translated styled formula at D4");
            const std::optional<fastxlsx::CellValue> reopened_a5 =
                reopened_sheet.try_cell("A5");
            check(reopened_a5.has_value() &&
                    reopened_a5->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a5->text_value() == "extra-c3",
                "insert_rows reopened output should read shifted source trailing cell at A5");
            const std::optional<fastxlsx::CellValue> reopened_c5 =
                reopened_sheet.try_cell("C5");
            check(reopened_c5.has_value() &&
                    reopened_c5->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c5->text_value() == "extra-c3",
                "insert_rows reopened output should read shifted dirty trailing cell at C5");
            check(!reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "insert_rows reopened output should keep old sparse coordinates absent");
        };
    check_reopened_shift_output(output, "insert_rows", inspect_insert_rows_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "insert_rows no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "insert_rows no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "insert_rows no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "insert_rows no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_rows no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_rows no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "insert_rows no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "insert_rows no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "insert_rows no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "insert_rows no-op save",
        inspect_insert_rows_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "insert_rows second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "insert_rows second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "insert_rows second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "insert_rows second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_rows second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_rows second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop, "insert_rows second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop, "insert_rows second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "insert_rows second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_rows second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows second no-op save should leave the source package unchanged");
    check_reopened_shift_output(second_noop_output, "insert_rows second no-op save",
        inspect_insert_rows_output);

    sheet.set_cell("E5", fastxlsx::CellValue::text("post-noop-insert-rows-styled"));
    check(sheet.has_pending_changes(),
        "insert_rows styled source formula post-noop edit should dirty the saved handle");
    check(sheet.cell_count() == 9,
        "insert_rows styled source formula post-noop edit should add one sparse cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 5, 5,
        "insert_rows styled source formula post-noop edit should expand bounds to E5");
    const std::optional<fastxlsx::CellValue> retained_formula = sheet.try_cell("D4");
    check(retained_formula.has_value() &&
            retained_formula->kind() == fastxlsx::CellValueKind::Formula &&
            retained_formula->text_value() == "A1+B1" &&
            retained_formula->has_style() &&
            retained_formula->style_id().value() == styled_formula_style.value(),
        "insert_rows styled source formula post-noop edit should preserve shifted formula style id");
    check(editor.pending_change_count() == 1,
        "insert_rows styled source formula post-noop edit should retain the prior handoff before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 9 &&
            editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "insert_rows styled source formula post-noop edit should report the dirty materialized sheet");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "insert_rows styled source formula post-noop save should clean the materialized handle");
    check(editor.pending_change_count() == 2,
        "insert_rows styled source formula post-noop save should record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_rows styled source formula post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_rows styled source formula post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_rows styled source formula post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "insert_rows styled source formula post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_rows styled source formula post-noop save should leave the no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows styled source formula post-noop save should leave the source package unchanged");
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, styled_formula_xml,
        "insert_rows styled source formula post-noop save should keep the styled formula cell");
    check_contains(post_noop_xml, R"(<c r="E5")",
        "insert_rows styled source formula post-noop save should write the post-noop edit");
    const auto inspect_insert_rows_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 9,
                "insert_rows styled source formula post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
                "insert_rows styled source formula post-noop reopened output should expose expanded bounds");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d4->text_value() == "A1+B1" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "insert_rows styled source formula post-noop reopened output should keep styled formula");
            const std::optional<fastxlsx::CellValue> reopened_e5 =
                reopened_sheet.try_cell("E5");
            check(reopened_e5.has_value() &&
                    reopened_e5->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e5->text_value() == "post-noop-insert-rows-styled",
                "insert_rows styled source formula post-noop reopened output should read post-noop edit");
            const std::optional<fastxlsx::CellValue> reopened_a5 =
                reopened_sheet.try_cell("A5");
            const std::optional<fastxlsx::CellValue> reopened_c5 =
                reopened_sheet.try_cell("C5");
            check(reopened_a5.has_value() && reopened_a5->text_value() == "extra-c3" &&
                    reopened_c5.has_value() && reopened_c5->text_value() == "extra-c3",
                "insert_rows styled source formula post-noop reopened output should keep shifted trailing cells");
            check(!reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "insert_rows styled source formula post-noop reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(post_noop_output,
        "insert_rows styled source formula post-noop save",
        inspect_insert_rows_post_noop_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(post_noop_noop_output);
    check(!sheet.has_pending_changes(),
        "insert_rows styled source formula post-noop noop save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "insert_rows styled source formula post-noop noop save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_rows styled source formula post-noop noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_rows styled source formula post-noop noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_rows styled source formula post-noop noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_post_noop_noop,
        "insert_rows styled source formula post-noop noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_post_noop_noop,
        "insert_rows styled source formula post-noop noop save");
    const auto post_noop_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_noop_output);
    check(post_noop_noop_entries == post_noop_entries,
        "insert_rows styled source formula post-noop noop output should match post-noop output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows styled source formula post-noop noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
        "insert_rows styled source formula post-noop noop save should leave prior post-noop output unchanged");
    check_reopened_shift_output(post_noop_noop_output,
        "insert_rows styled source formula post-noop noop save",
        inspect_insert_rows_post_noop_output);
}

void test_public_worksheet_editor_insert_rows_preserves_shifted_value_only_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-insert-rows-value-only-style-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-value-only-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-value-only-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-value-only-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-value-only-style-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell_value("D2", fastxlsx::CellValue::text("value-only-styled"));
    const std::optional<fastxlsx::CellValue> value_only_d2 = sheet.try_cell("D2");
    check(value_only_d2.has_value() &&
            value_only_d2->kind() == fastxlsx::CellValueKind::Text &&
            value_only_d2->text_value() == "value-only-styled" &&
            value_only_d2->has_style() &&
            value_only_d2->style_id().value() == styled_formula_style.value(),
        "set_cell_value should preserve the source style before insert_rows shifts it");

    sheet.insert_rows(2, 2);

    check(sheet.cell_count() == 7,
        "insert_rows should keep value-only shifted sparse count stable");
    check_cell_range_equals(sheet.used_range(), 1, 1, 5, 4,
        "insert_rows should refresh bounds for shifted value-only styled cells");
    const std::optional<fastxlsx::CellValue> shifted_value = sheet.try_cell("D4");
    check(shifted_value.has_value() &&
            shifted_value->kind() == fastxlsx::CellValueKind::Text &&
            shifted_value->text_value() == "value-only-styled" &&
            shifted_value->has_style() &&
            shifted_value->style_id().value() == styled_formula_style.value(),
        "insert_rows should move value-only cells with the preserved source style id");
    check(!sheet.try_cell("D2").has_value(),
        "insert_rows should remove the old value-only styled coordinate");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_four = sheet.row_cells(4);
    check(shifted_row_four.size() == 4,
        "insert_rows value-only row_cells should expose the shifted source row");
    check(shifted_row_four[3].reference.row == 4 &&
            shifted_row_four[3].reference.column == 4 &&
            shifted_row_four[3].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_four[3].value.text_value() == "value-only-styled" &&
            shifted_row_four[3].value.has_style() &&
            shifted_row_four[3].value.style_id().value() == styled_formula_style.value(),
        "insert_rows value-only row_cells should keep the shifted style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "insert_rows value-only style pre-save shift summary");
    check(editor.pending_materialized_cell_count() == 7,
        "insert_rows value-only style should keep aggregate materialized cell count stable");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "insert_rows value-only style should keep aggregate materialized memory stable");
    check(!editor.last_edit_error().has_value(),
        "successful insert_rows value-only style shift should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows value-only style save_as should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "insert_rows value-only style save_as should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_text =
        R"(<c r="D4" s=")" + std::to_string(styled_formula_style.value())
        + R"(" t="inlineStr"><is><t>value-only-styled</t></is></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "insert_rows value-only style save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_text,
        "insert_rows value-only style save_as should write shifted text with source style id");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "insert_rows value-only style save_as should omit the old coordinate");
    check_not_contains(worksheet_xml, R"(<f>)",
        "insert_rows value-only style save_as should not write any formula records");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "insert_rows value-only style should preserve untouched worksheets");
    const auto inspect_value_only_shift_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "insert_rows value-only style reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
                "insert_rows value-only style reopened output should keep shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d4->text_value() == "value-only-styled" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "insert_rows value-only style reopened output should preserve shifted source style");
            check(!reopened_sheet.try_cell("D2").has_value(),
                "insert_rows value-only style reopened output should keep the old coordinate absent");
            const std::optional<fastxlsx::CellValue> reopened_a5 =
                reopened_sheet.try_cell("A5");
            check(reopened_a5.has_value() && reopened_a5->text_value() == "extra-c3",
                "insert_rows value-only style reopened output should keep shifted trailing source cells");
        };
    check_reopened_shift_output(output, "insert_rows value-only style",
        inspect_value_only_shift_output);
    check_reopened_untouched_keep_me_output(output, "insert_rows value-only style");
    const auto noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        "insert_rows value-only style no-op save",
        inspect_value_only_shift_output);
    const auto second_noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        "insert_rows value-only style second no-op save",
        inspect_value_only_shift_output);
    check(second_noop_entries == noop_entries,
        "insert_rows value-only style second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_rows value-only style second no-op save should leave first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "insert_rows value-only style second no-op save should leave the materialized output unchanged");
    const auto inspect_value_only_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "insert_rows value-only style post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
                "insert_rows value-only style post-noop output should expand to the later edit");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d4->text_value() == "value-only-styled" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "insert_rows value-only style post-noop output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_e5 =
                reopened_sheet.try_cell("E5");
            check(reopened_e5.has_value() &&
                    reopened_e5->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e5->text_value() == "post-noop-insert-rows-value-only-style",
                "insert_rows value-only style post-noop output should include the later edit");
            const std::optional<fastxlsx::CellValue> reopened_a5 =
                reopened_sheet.try_cell("A5");
            check(reopened_a5.has_value() &&
                    reopened_a5->text_value() == "extra-c3",
                "insert_rows value-only style post-noop output should keep shifted trailing source cells");
            check(!reopened_sheet.try_cell("D2").has_value(),
                "insert_rows value-only style post-noop output should keep the old coordinate absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "insert_rows value-only style post-noop edit save",
        8,
        [&sheet]() {
            sheet.set_cell("E5",
                fastxlsx::CellValue::text("post-noop-insert-rows-value-only-style"));
        },
        inspect_value_only_post_noop_output);
}

void test_public_worksheet_editor_insert_rows_preserves_shifted_clear_value_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-insert-rows-clear-value-style-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-clear-value-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-clear-value-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-clear-value-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-clear-value-style-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.clear_cell_value("D2");
    const std::optional<fastxlsx::CellValue> cleared_d2 = sheet.try_cell("D2");
    check(cleared_d2.has_value() &&
            cleared_d2->kind() == fastxlsx::CellValueKind::Blank &&
            cleared_d2->has_style() &&
            cleared_d2->style_id().value() == styled_formula_style.value(),
        "clear_cell_value should preserve the source style before insert_rows shifts it");

    sheet.insert_rows(2, 2);

    check(sheet.cell_count() == 7,
        "insert_rows should keep cleared shifted sparse count stable");
    check_cell_range_equals(sheet.used_range(), 1, 1, 5, 4,
        "insert_rows should refresh bounds for shifted cleared styled cells");
    const std::optional<fastxlsx::CellValue> shifted_blank = sheet.try_cell("D4");
    check(shifted_blank.has_value() &&
            shifted_blank->kind() == fastxlsx::CellValueKind::Blank &&
            shifted_blank->has_style() &&
            shifted_blank->style_id().value() == styled_formula_style.value(),
        "insert_rows should move cleared cells with the preserved source style id");
    check(!sheet.try_cell("D2").has_value(),
        "insert_rows should remove the old cleared styled coordinate");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_four = sheet.row_cells(4);
    check(shifted_row_four.size() == 4,
        "insert_rows cleared row_cells should expose the shifted source row");
    check(shifted_row_four[3].reference.row == 4 &&
            shifted_row_four[3].reference.column == 4 &&
            shifted_row_four[3].value.kind() == fastxlsx::CellValueKind::Blank &&
            shifted_row_four[3].value.has_style() &&
            shifted_row_four[3].value.style_id().value() == styled_formula_style.value(),
        "insert_rows cleared row_cells should keep the shifted style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "insert_rows cleared style pre-save shift summary");
    check(editor.pending_materialized_cell_count() == 7,
        "insert_rows cleared style should keep aggregate materialized cell count stable");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "insert_rows cleared style should keep aggregate materialized memory stable");
    check(!editor.last_edit_error().has_value(),
        "successful insert_rows cleared style shift should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows cleared style save_as should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "insert_rows cleared style save_as should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_blank =
        R"(<c r="D4" s=")" + std::to_string(styled_formula_style.value()) + R"("/>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "insert_rows cleared style save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_blank,
        "insert_rows cleared style save_as should write shifted blank with source style id");
    check_contains(worksheet_xml, R"(<c r="A5" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "insert_rows cleared style save_as should keep shifted trailing source cells");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "insert_rows cleared style save_as should omit the old coordinate");
    check_not_contains(worksheet_xml, R"(<f>A1+B1</f>)",
        "insert_rows cleared style save_as should omit the cleared source formula");
    check_not_contains(worksheet_xml, R"(<f>)",
        "insert_rows cleared style save_as should not write any formula records");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "insert_rows cleared style should preserve untouched worksheets");
    const auto inspect_cleared_shift_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "insert_rows cleared style reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
                "insert_rows cleared style reopened output should keep shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Blank &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "insert_rows cleared style reopened output should preserve shifted source style");
            check(!reopened_sheet.try_cell("D2").has_value(),
                "insert_rows cleared style reopened output should keep the old coordinate absent");
            const std::optional<fastxlsx::CellValue> reopened_a5 =
                reopened_sheet.try_cell("A5");
            check(reopened_a5.has_value() && reopened_a5->text_value() == "extra-c3",
                "insert_rows cleared style reopened output should keep shifted trailing source cells");
        };
    check_reopened_shift_output(output, "insert_rows cleared style",
        inspect_cleared_shift_output);
    check_reopened_untouched_keep_me_output(output, "insert_rows cleared style");
    const auto noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        "insert_rows cleared style no-op save",
        inspect_cleared_shift_output);
    const auto second_noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        "insert_rows cleared style second no-op save",
        inspect_cleared_shift_output);
    check(second_noop_entries == noop_entries,
        "insert_rows cleared style second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_rows cleared style second no-op save should leave first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "insert_rows cleared style second no-op save should leave the materialized output unchanged");
    const auto inspect_cleared_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "insert_rows cleared style post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
                "insert_rows cleared style post-noop output should expand to the later edit");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Blank &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "insert_rows cleared style post-noop output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_e5 =
                reopened_sheet.try_cell("E5");
            check(reopened_e5.has_value() &&
                    reopened_e5->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e5->text_value() == "post-noop-insert-rows-cleared-style",
                "insert_rows cleared style post-noop output should include the later edit");
            const std::optional<fastxlsx::CellValue> reopened_a5 =
                reopened_sheet.try_cell("A5");
            check(reopened_a5.has_value() &&
                    reopened_a5->text_value() == "extra-c3",
                "insert_rows cleared style post-noop output should keep shifted trailing source cells");
            check(!reopened_sheet.try_cell("D2").has_value(),
                "insert_rows cleared style post-noop output should keep the old coordinate absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "insert_rows cleared style post-noop edit save",
        8,
        [&sheet]() {
            sheet.set_cell("E5",
                fastxlsx::CellValue::text("post-noop-insert-rows-cleared-style"));
        },
        inspect_cleared_post_noop_output);
}

void test_public_worksheet_editor_full_calculation_preserves_insert_rows_shift()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-rows-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-insert-rows-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-insert-rows-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-rows-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-rows-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(3, 3, fastxlsx::CellValue::text("extra-c3"));
    sheet.insert_rows(2, 2);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 8,
        "full-calc insert_rows setup should keep shifted sparse count");
    check(editor.pending_change_count() == 0,
        "full-calc insert_rows setup should not queue a Patch handoff before save_as");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "full-calc insert_rows setup should report Data dirty");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "full-calc insert_rows setup should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "full-calc insert_rows setup should report shifted sparse memory");

    editor.request_full_calculation();

    check(!editor.last_edit_error().has_value(),
        "request_full_calculation after insert_rows should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "request_full_calculation after insert_rows should add one metadata edit");
    check(sheet.has_pending_changes(),
        "request_full_calculation after insert_rows should keep the shifted sheet dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "request_full_calculation after insert_rows should preserve dirty materialized names");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "request_full_calculation after insert_rows should preserve dirty sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "request_full_calculation after insert_rows should preserve dirty sparse memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 1, "request_full_calculation after insert_rows dirty summary");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc insert_rows save_as should clean the shifted materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc insert_rows save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc insert_rows save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc insert_rows save_as should clear dirty materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc insert_rows save_as should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc insert_rows save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "full-calc insert_rows save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc insert_rows save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "full-calc insert_rows save_as should project the shifted sparse dimension");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc insert_rows save_as should write shifted styled formula text");
    check_contains(worksheet_xml, R"(<c r="A5")",
        "full-calc insert_rows save_as should write shifted source-backed trailing cell");
    check_contains(worksheet_xml, R"(<c r="C5")",
        "full-calc insert_rows save_as should write shifted dirty trailing cell");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc insert_rows save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "full-calc insert_rows save_as should omit old dirty coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc insert_rows save_as should preserve untouched worksheets");

    const auto inspect_full_calc_insert_rows_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "full-calc insert_rows reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
                "full-calc insert_rows reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d4->text_value() == "A1+B1" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "full-calc insert_rows reopened output should read shifted styled formula");
            const std::optional<fastxlsx::CellValue> reopened_a5 =
                reopened_sheet.try_cell("A5");
            check(reopened_a5.has_value() &&
                    reopened_a5->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a5->text_value() == "extra-c3",
                "full-calc insert_rows reopened output should read shifted source trailing cell");
            const std::optional<fastxlsx::CellValue> reopened_c5 =
                reopened_sheet.try_cell("C5");
            check(reopened_c5.has_value() &&
                    reopened_c5->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c5->text_value() == "extra-c3",
                "full-calc insert_rows reopened output should read shifted dirty trailing cell");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_five =
                reopened_sheet.row_cells(5);
            check(reopened_row_five.size() == 2 &&
                    reopened_row_five[0].reference.row == 5 &&
                    reopened_row_five[0].reference.column == 1 &&
                    reopened_row_five[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_five[0].value.text_value() == "extra-c3" &&
                    reopened_row_five[1].reference.row == 5 &&
                    reopened_row_five[1].reference.column == 3 &&
                    reopened_row_five[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_five[1].value.text_value() == "extra-c3",
                "full-calc insert_rows reopened row_cells should expose shifted trailing cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 4 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_four[0].value.text_value() == "A1+B1" &&
                    reopened_column_four[0].value.has_style() &&
                    reopened_column_four[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc insert_rows reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "full-calc insert_rows reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "full-calc insert_rows",
        inspect_full_calc_insert_rows_output);
    check_reopened_untouched_keep_me_output(
        output, "full-calc insert_rows Untouched");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc insert_rows no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc insert_rows no-op save should not record another workbook or materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc insert_rows no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc insert_rows no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc insert_rows no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_rows no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc insert_rows no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc insert_rows no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc insert_rows no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc insert_rows no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "full-calc insert_rows no-op save",
        inspect_full_calc_insert_rows_output);
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc insert_rows no-op Untouched");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc insert_rows second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc insert_rows second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc insert_rows second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc insert_rows second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc insert_rows second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_rows second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "full-calc insert_rows second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "full-calc insert_rows second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc insert_rows second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc insert_rows second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc insert_rows second no-op save should leave materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc insert_rows second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "full-calc insert_rows second no-op save",
        inspect_full_calc_insert_rows_output);
    check_reopened_untouched_keep_me_output(
        second_noop_output, "full-calc insert_rows second no-op Untouched");
    const auto inspect_full_calc_insert_rows_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 9,
                "full-calc insert_rows post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
                "full-calc insert_rows post-noop output should expand to the later edit");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d4->text_value() == "A1+B1" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "full-calc insert_rows post-noop output should keep the shifted styled formula");
            const std::optional<fastxlsx::CellValue> reopened_a5 =
                reopened_sheet.try_cell("A5");
            const std::optional<fastxlsx::CellValue> reopened_c5 =
                reopened_sheet.try_cell("C5");
            const std::optional<fastxlsx::CellValue> reopened_e5 =
                reopened_sheet.try_cell("E5");
            check(reopened_a5.has_value() &&
                    reopened_a5->text_value() == "extra-c3" &&
                    reopened_c5.has_value() &&
                    reopened_c5->text_value() == "extra-c3" &&
                    reopened_e5.has_value() &&
                    reopened_e5->text_value() == "post-noop-full-calc-insert-rows",
                "full-calc insert_rows post-noop output should read shifted and later cells");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "full-calc insert_rows post-noop output should keep old coordinates absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "full-calc insert_rows post-noop edit save",
        9,
        [&sheet]() {
            sheet.set_cell("E5",
                fastxlsx::CellValue::text("post-noop-full-calc-insert-rows"));
        },
        inspect_full_calc_insert_rows_post_noop_output);
    const auto post_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_output);
    check_contains(post_noop_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc insert_rows post-noop save should keep fullCalcOnLoad metadata");
    check(post_noop_entries.find("xl/calcChain.xml") == post_noop_entries.end(),
        "full-calc insert_rows post-noop save should not invent calcChain.xml");
}

void test_public_worksheet_editor_full_calculation_preserves_insert_rows_failed_save_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-rows-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-insert-rows-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-insert-rows-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-rows-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(3, 3, fastxlsx::CellValue::text("extra-c3"));
    sheet.insert_rows(2, 2);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 8,
        "full-calc insert_rows failed save setup should keep shifted sparse count");

    editor.request_full_calculation();

    const auto source_entries_before_failed_save =
        fastxlsx::test::read_zip_entries(source);
    const auto check_dirty_insert_rows_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep diagnostics clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued workbook metadata edit before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the shifted worksheet dirty");
        check(sheet.cell_count() == dirty_cell_count,
            label + " should preserve the shifted sparse count");
        check(sheet.estimated_memory_usage() == dirty_memory_usage,
            label + " should preserve the shifted materialized memory estimate");
        check_cell_range_equals(sheet.used_range(), 1, 1, 5, 4,
            label + " should expose shifted bounds");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"Data"},
            label + " should report Data dirty");
        check(editor.pending_materialized_cell_count() == dirty_cell_count,
            label + " should report the shifted sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            label + " should report the shifted sparse memory");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, label + " dirty materialized summary");

        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D4");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "A1+B1" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep translated formula and style id in memory");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A4").text_value() == "placeholder-a2" &&
                sheet.get_cell("B4").text_value() == "row2-gap-b2" &&
                sheet.get_cell("C4").text_value() == "row2-gap-c2" &&
                sheet.get_cell("A5").text_value() == "extra-c3" &&
                sheet.get_cell("C5").text_value() == "extra-c3",
            label + " should keep shifted source and dirty rows in memory");
        check(!sheet.try_cell("D2").has_value() &&
                !sheet.try_cell("A3").has_value() &&
                !sheet.try_cell("C3").has_value(),
            label + " should keep old and inserted coordinates absent");
    };

    check_dirty_insert_rows_session(
        "full-calc insert_rows failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "full-calc insert_rows failed save should reject exact source overwrite");
    check_dirty_insert_rows_session(
        "full-calc insert_rows failed save rejected source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc insert_rows failed save should leave source package unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc insert_rows failed save safe retry should clean the shifted sheet");
    check(editor.has_pending_changes(),
        "full-calc insert_rows failed save safe retry should retain staged public changes");
    check(editor.pending_change_count() == 2,
        "full-calc insert_rows failed save safe retry should count metadata plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc insert_rows failed save safe retry should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc insert_rows failed save safe retry should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc insert_rows failed save safe retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_rows failed save safe retry should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc insert_rows failed save safe retry should leave source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "full-calc insert_rows failed save safe retry should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc insert_rows failed save safe retry should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "full-calc insert_rows failed save safe retry should project shifted bounds");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc insert_rows failed save safe retry should write shifted styled formula");
    check_contains(worksheet_xml, R"(<c r="A5" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "full-calc insert_rows failed save safe retry should write shifted source trailing cell");
    check_contains(worksheet_xml, R"(<c r="C5" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "full-calc insert_rows failed save safe retry should write shifted dirty trailing cell");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc insert_rows failed save safe retry should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc insert_rows failed save safe retry should omit old source coordinate");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "full-calc insert_rows failed save safe retry should omit old dirty coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc insert_rows failed save safe retry should preserve untouched worksheets");

    const auto inspect_full_calc_insert_rows_failed_save_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "full-calc insert_rows failed save no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
                "full-calc insert_rows failed save no-op reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d4->text_value() == "A1+B1" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "full-calc insert_rows failed save no-op reopened output should read shifted formula style");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("B1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B4").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("C4").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A5").text_value() == "extra-c3" &&
                    reopened_sheet.get_cell("C5").text_value() == "extra-c3",
                "full-calc insert_rows failed save no-op reopened output should read shifted source and dirty rows");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_five =
                reopened_sheet.row_cells(5);
            check(reopened_row_five.size() == 2 &&
                    reopened_row_five[0].reference.row == 5 &&
                    reopened_row_five[0].reference.column == 1 &&
                    reopened_row_five[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_five[0].value.text_value() == "extra-c3" &&
                    reopened_row_five[1].reference.row == 5 &&
                    reopened_row_five[1].reference.column == 3 &&
                    reopened_row_five[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_five[1].value.text_value() == "extra-c3",
                "full-calc insert_rows failed save no-op reopened row_cells should expose shifted trailing cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 4 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_four[0].value.text_value() == "A1+B1" &&
                    reopened_column_four[0].value.has_style() &&
                    reopened_column_four[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc insert_rows failed save no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "full-calc insert_rows failed save no-op reopened output should keep old coordinates absent");
        };

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc insert_rows failed save no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc insert_rows failed save no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc insert_rows failed save no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc insert_rows failed save no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_rows failed save no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc insert_rows failed save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc insert_rows failed save no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc insert_rows failed save no-op output should match safe retry output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc insert_rows failed save no-op save should leave safe retry output unchanged");
    check_reopened_shift_output(
        noop_output,
        "full-calc insert_rows failed save no-op save",
        inspect_full_calc_insert_rows_failed_save_output);
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc insert_rows failed save no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc insert_rows failed save source after no-op");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc insert_rows failed save second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc insert_rows failed save second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc insert_rows failed save second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc insert_rows failed save second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_rows failed save second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "full-calc insert_rows failed save second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "full-calc insert_rows failed save second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc insert_rows failed save second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc insert_rows failed save second no-op save should leave safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc insert_rows failed save second no-op save should leave first no-op output unchanged");
    check_reopened_shift_output(
        second_noop_output,
        "full-calc insert_rows failed save second no-op save",
        inspect_full_calc_insert_rows_failed_save_output);
    check_reopened_untouched_keep_me_output(
        second_noop_output,
        "full-calc insert_rows failed save second no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc insert_rows failed save source after second no-op");
}

void test_public_worksheet_editor_full_calculation_before_insert_rows_styled_formula_shift()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-rows-styled-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-rows-styled-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-rows-styled-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-rows-styled-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-rows-styled-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_rows styled formula setup should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "full-calc before insert_rows styled formula setup should queue one workbook metadata edit");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before insert_rows styled formula setup should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before insert_rows styled formula setup should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_rows styled formula setup should not expose dirty materialized memory");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check(!sheet.has_pending_changes(),
        "worksheet() after full-calc before insert_rows styled formula should materialize cleanly");
    check(editor.pending_change_count() == 1,
        "clean materialization after full-calc before insert_rows styled formula should keep metadata edit count");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "clean materialization after full-calc before insert_rows styled formula should keep dirty diagnostics clear");

    sheet.insert_rows(2, 2);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 7,
        "full-calc before insert_rows styled formula should keep shifted sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 5, 4,
        "full-calc before insert_rows styled formula should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D4");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+B1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "full-calc before insert_rows styled formula should translate formula and preserve style id");
    check(editor.pending_change_count() == 1,
        "full-calc before insert_rows styled formula should not flush materialized state before save_as");
    check(sheet.has_pending_changes(),
        "full-calc before insert_rows styled formula should leave the shifted sheet dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "full-calc before insert_rows styled formula should report Data dirty");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "full-calc before insert_rows styled formula should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "full-calc before insert_rows styled formula should report shifted sparse memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 1,
        "full-calc before insert_rows styled formula pre-save dirty summary");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc before insert_rows styled formula save_as should clean the shifted materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_rows styled formula save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before insert_rows styled formula save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before insert_rows styled formula save_as should clear dirty materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_rows styled formula save_as should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before insert_rows styled formula save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "full-calc before insert_rows styled formula save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc before insert_rows styled formula save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "full-calc before insert_rows styled formula save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "full-calc before insert_rows styled formula save_as should keep source row one");
    check_contains(worksheet_xml, R"(<c r="A4")",
        "full-calc before insert_rows styled formula save_as should write shifted source row two");
    check_contains(worksheet_xml, R"(<c r="A5")",
        "full-calc before insert_rows styled formula save_as should write shifted source row three");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc before insert_rows styled formula save_as should write shifted styled formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc before insert_rows styled formula save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc before insert_rows styled formula save_as should omit old trailing coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc before insert_rows styled formula should preserve untouched worksheets");

    const auto inspect_full_calc_before_insert_rows_styled_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc before insert_rows styled formula reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
                "full-calc before insert_rows styled formula reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d4->text_value() == "A1+B1" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "full-calc before insert_rows styled formula reopened output should read shifted styled formula");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("B1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B4").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("C4").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A5").text_value() == "extra-c3",
                "full-calc before insert_rows styled formula reopened output should read shifted source rows");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_four =
                reopened_sheet.row_cells(4);
            check(reopened_row_four.size() == 4 &&
                    reopened_row_four[0].reference.row == 4 &&
                    reopened_row_four[0].reference.column == 1 &&
                    reopened_row_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_four[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_four[1].reference.row == 4 &&
                    reopened_row_four[1].reference.column == 2 &&
                    reopened_row_four[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_four[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_four[2].reference.row == 4 &&
                    reopened_row_four[2].reference.column == 3 &&
                    reopened_row_four[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_four[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_four[3].reference.row == 4 &&
                    reopened_row_four[3].reference.column == 4 &&
                    reopened_row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_four[3].value.text_value() == "A1+B1" &&
                    reopened_row_four[3].value.has_style() &&
                    reopened_row_four[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_rows styled formula reopened row_cells should expose shifted row two");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 4 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_four[0].value.text_value() == "A1+B1" &&
                    reopened_column_four[0].value.has_style() &&
                    reopened_column_four[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_rows styled formula reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value() &&
                    !reopened_sheet.try_cell("C5").has_value(),
                "full-calc before insert_rows styled formula reopened output should keep old and non-dirty coordinates absent");
        };
    check_reopened_shift_output(output,
        "full-calc before insert_rows styled formula",
        inspect_full_calc_before_insert_rows_styled_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_rows styled formula no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_rows styled formula no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before insert_rows styled formula no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_rows styled formula no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_rows styled formula no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "full-calc before insert_rows styled formula no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "full-calc before insert_rows styled formula no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc before insert_rows styled formula no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before insert_rows styled formula no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output,
        "full-calc before insert_rows styled formula no-op save",
        inspect_full_calc_before_insert_rows_styled_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_rows styled formula second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_rows styled formula second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before insert_rows styled formula second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_rows styled formula second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_rows styled formula second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "full-calc before insert_rows styled formula second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "full-calc before insert_rows styled formula second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc before insert_rows styled formula second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before insert_rows styled formula second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before insert_rows styled formula second no-op save should leave materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc before insert_rows styled formula second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "full-calc before insert_rows styled formula second no-op save",
        inspect_full_calc_before_insert_rows_styled_output);
    const auto inspect_full_calc_before_insert_rows_styled_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "full-calc before insert_rows styled formula post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
                "full-calc before insert_rows styled formula post-noop output should expand to the later edit");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d4->text_value() == "A1+B1" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "full-calc before insert_rows styled formula post-noop output should keep the shifted formula style");
            const std::optional<fastxlsx::CellValue> reopened_e5 =
                reopened_sheet.try_cell("E5");
            check(reopened_e5.has_value() &&
                    reopened_e5->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e5->text_value() == "post-noop-full-calc-before-insert-rows",
                "full-calc before insert_rows styled formula post-noop output should include the later edit");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("A5").text_value() == "extra-c3",
                "full-calc before insert_rows styled formula post-noop output should read shifted source rows");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value() &&
                    !reopened_sheet.try_cell("C5").has_value(),
                "full-calc before insert_rows styled formula post-noop output should keep old and non-dirty coordinates absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "full-calc before insert_rows styled formula post-noop edit save",
        8,
        [&sheet]() {
            sheet.set_cell("E5",
                fastxlsx::CellValue::text("post-noop-full-calc-before-insert-rows"));
        },
        inspect_full_calc_before_insert_rows_styled_post_noop_output);
    const auto post_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_output);
    check_contains(post_noop_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc before insert_rows styled formula post-noop save should keep fullCalcOnLoad metadata");
    check(post_noop_entries.find("xl/calcChain.xml") == post_noop_entries.end(),
        "full-calc before insert_rows styled formula post-noop save should not invent calcChain.xml");
}

void test_public_worksheet_editor_full_calculation_before_insert_rows_styled_formula_failed_save_preserves_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-rows-styled-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-rows-styled-failed-save-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-rows-styled-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-rows-styled-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.insert_rows(2, 2);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    const auto source_entries_before_failed_save =
        fastxlsx::test::read_zip_entries(source);
    check(dirty_cell_count == 7,
        "full-calc before insert_rows styled formula failed save setup should keep shifted sparse count");

    const auto check_dirty_insert_rows_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep diagnostics clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued workbook metadata edit before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the shifted worksheet dirty");
        check(sheet.cell_count() == dirty_cell_count,
            label + " should preserve the shifted sparse count");
        check(sheet.estimated_memory_usage() == dirty_memory_usage,
            label + " should preserve the shifted materialized memory estimate");
        check_cell_range_equals(sheet.used_range(), 1, 1, 5, 4,
            label + " should expose shifted bounds");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"Data"},
            label + " should report Data dirty");
        check(editor.pending_materialized_cell_count() == dirty_cell_count,
            label + " should report the shifted sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            label + " should report the shifted sparse memory");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, label + " dirty materialized summary");

        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D4");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "A1+B1" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep translated formula and style id in memory");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A4").text_value() == "placeholder-a2" &&
                sheet.get_cell("B4").text_value() == "row2-gap-b2" &&
                sheet.get_cell("C4").text_value() == "row2-gap-c2" &&
                sheet.get_cell("A5").text_value() == "extra-c3",
            label + " should keep shifted source rows in memory");
        check(!sheet.try_cell("D2").has_value() &&
                !sheet.try_cell("A3").has_value(),
            label + " should keep old and inserted coordinates absent");
    };

    check_dirty_insert_rows_session(
        "full-calc before insert_rows styled formula failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "full-calc before insert_rows styled formula failed save should reject exact source overwrite");
    check_dirty_insert_rows_session(
        "full-calc before insert_rows styled formula failed save rejected source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc before insert_rows styled formula failed save should leave source package unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc before insert_rows styled formula failed save safe retry should clean the shifted sheet");
    check(editor.has_pending_changes(),
        "full-calc before insert_rows styled formula failed save safe retry should retain staged public changes");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_rows styled formula failed save safe retry should count metadata plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_rows styled formula failed save safe retry should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc before insert_rows styled formula failed save safe retry should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_rows styled formula failed save safe retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_rows styled formula failed save safe retry should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc before insert_rows styled formula failed save safe retry should leave source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "full-calc before insert_rows styled formula failed save safe retry should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc before insert_rows styled formula failed save safe retry should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "full-calc before insert_rows styled formula failed save safe retry should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "full-calc before insert_rows styled formula failed save safe retry should keep source row one");
    check_contains(worksheet_xml, R"(<c r="A4")",
        "full-calc before insert_rows styled formula failed save safe retry should write shifted source row two");
    check_contains(worksheet_xml, R"(<c r="A5" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "full-calc before insert_rows styled formula failed save safe retry should write shifted source row three");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc before insert_rows styled formula failed save safe retry should write shifted styled formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc before insert_rows styled formula failed save safe retry should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc before insert_rows styled formula failed save safe retry should omit inserted row coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc before insert_rows styled formula failed save safe retry should preserve untouched worksheets");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_rows styled formula failed save no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_rows styled formula failed save no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before insert_rows styled formula failed save no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_rows styled formula failed save no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_rows styled formula failed save no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "full-calc before insert_rows styled formula failed save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "full-calc before insert_rows styled formula failed save no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc before insert_rows styled formula failed save no-op output should match safe retry output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before insert_rows styled formula failed save no-op save should leave safe retry output unchanged");
    check_reopened_shift_output(
        noop_output,
        "full-calc before insert_rows styled formula failed save no-op save",
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc before insert_rows styled formula failed save no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
                "full-calc before insert_rows styled formula failed save no-op reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d4 = reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d4->text_value() == "A1+B1" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "full-calc before insert_rows styled formula failed save no-op reopened output should read shifted formula style");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("B1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("A5").text_value() == "extra-c3",
                "full-calc before insert_rows styled formula failed save no-op reopened output should read shifted source rows");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_four =
                reopened_sheet.row_cells(4);
            check(reopened_row_four.size() == 4 &&
                    reopened_row_four[0].reference.row == 4 &&
                    reopened_row_four[0].reference.column == 1 &&
                    reopened_row_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_four[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_four[1].reference.row == 4 &&
                    reopened_row_four[1].reference.column == 2 &&
                    reopened_row_four[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_four[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_four[2].reference.row == 4 &&
                    reopened_row_four[2].reference.column == 3 &&
                    reopened_row_four[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_four[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_four[3].reference.row == 4 &&
                    reopened_row_four[3].reference.column == 4 &&
                    reopened_row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_four[3].value.text_value() == "A1+B1" &&
                    reopened_row_four[3].value.has_style() &&
                    reopened_row_four[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_rows styled formula failed save no-op reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 4 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_four[0].value.text_value() == "A1+B1" &&
                    reopened_column_four[0].value.has_style() &&
                    reopened_column_four[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_rows styled formula failed save no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc before insert_rows styled formula failed save no-op reopened output should keep old coordinates absent");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_rows styled formula failed save second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_rows styled formula failed save second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before insert_rows styled formula failed save second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_rows styled formula failed save second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_rows styled formula failed save second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "full-calc before insert_rows styled formula failed save second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "full-calc before insert_rows styled formula failed save second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc before insert_rows styled formula failed save second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before insert_rows styled formula failed save second no-op save should leave safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc before insert_rows styled formula failed save second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(
        second_noop_output,
        "full-calc before insert_rows styled formula failed save second no-op save",
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc before insert_rows styled formula failed save second no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
                "full-calc before insert_rows styled formula failed save second no-op reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d4 =
                reopened_sheet.try_cell("D4");
            check(reopened_d4.has_value() &&
                    reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d4->text_value() == "A1+B1" &&
                    reopened_d4->has_style() &&
                    reopened_d4->style_id().value() == styled_formula_style.value(),
                "full-calc before insert_rows styled formula failed save second no-op reopened output should read shifted formula style");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("B1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("A5").text_value() == "extra-c3",
                "full-calc before insert_rows styled formula failed save second no-op reopened output should read shifted source rows");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 4 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_four[0].value.text_value() == "A1+B1" &&
                    reopened_column_four[0].value.has_style() &&
                    reopened_column_four[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_rows styled formula failed save second no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc before insert_rows styled formula failed save second no-op reopened output should keep old coordinates absent");
        });
    check_reopened_untouched_keep_me_output(
        noop_output,
        "full-calc before insert_rows styled formula failed save no-op Untouched");
    check_reopened_untouched_keep_me_output(
        second_noop_output,
        "full-calc before insert_rows styled formula failed save second no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc before insert_rows styled formula failed save source after no-op");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc before insert_rows styled formula failed save source after second no-op");
}

void test_public_worksheet_editor_insert_rows_shifted_sparse_snapshot()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-insert-rows-snapshot-source.xlsx",
            styled_formula_style);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 2);

    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells();
    check(cells.size() == 7,
        "insert_rows sparse_cells should keep the shifted sparse record count");
    check(cells[0].reference.row == 1 && cells[0].reference.column == 1 &&
            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[0].value.text_value() == "placeholder-a1",
        "insert_rows sparse_cells should keep row-major source-backed A1 first");
    check(cells[1].reference.row == 1 && cells[1].reference.column == 2 &&
            cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
            cells[1].value.number_value() == 1.0,
        "insert_rows sparse_cells should keep same-row cells after A1");
    check(cells[2].reference.row == 4 && cells[2].reference.column == 1 &&
            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[2].value.text_value() == "placeholder-a2",
        "insert_rows sparse_cells should expose the shifted source-backed row");
    check(cells[3].reference.row == 4 && cells[3].reference.column == 2 &&
            cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[3].value.text_value() == "row2-gap-b2",
        "insert_rows sparse_cells should preserve the shifted filler cell");
    check(cells[4].reference.row == 4 && cells[4].reference.column == 3 &&
            cells[4].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[4].value.text_value() == "row2-gap-c2",
        "insert_rows sparse_cells should preserve the other shifted filler cell");
    check(cells[5].reference.row == 4 && cells[5].reference.column == 4 &&
            cells[5].value.kind() == fastxlsx::CellValueKind::Formula &&
            cells[5].value.text_value() == "A1+B1" &&
            cells[5].value.has_style() &&
            cells[5].value.style_id().value() == styled_formula_style.value(),
        "insert_rows sparse_cells should translate the shifted formula cell and keep its style");
    check(cells[6].reference.row == 5 && cells[6].reference.column == 1 &&
            cells[6].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[6].value.text_value() == "extra-c3",
        "insert_rows sparse_cells should keep later dirty rows after the shift");
}

void test_public_worksheet_editor_delete_rows_shifts_sparse_records()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-delete-rows-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-post-noop-output.xlsx");
    const std::filesystem::path post_noop_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-delete-rows-post-noop-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 3, fastxlsx::CellValue::formula("A2+B4"));
    sheet.set_cell(4, 2, fastxlsx::CellValue::text("tail-b4"));
    sheet.delete_rows(1, 1);

    check(sheet.cell_count() == 3,
        "delete_rows should remove represented records in the deleted sparse rows");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2",
        "delete_rows should shift later source-backed rows upward");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("C3");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula
            && shifted_formula.text_value() == "A1+B3",
        "delete_rows should translate moved formula text by the row shift");
    check(sheet.get_cell("B3").text_value() == "tail-b4",
        "delete_rows should shift later dirty rows upward");
    check(!sheet.try_cell("B1").has_value(),
        "delete_rows should remove represented cells from the deleted row");
    check(!sheet.try_cell("A2").has_value(),
        "delete_rows should remove old shifted source coordinates");
    check(!sheet.try_cell("C4").has_value(),
        "delete_rows should remove the old shifted formula coordinate");
    check(!sheet.try_cell("B4").has_value(),
        "delete_rows should remove old shifted dirty coordinates");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 3,
        "delete_rows should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_three = sheet.row_cells(3);
    check(shifted_row_three.size() == 2,
        "delete_rows row_cells should expose the shifted row snapshot");
    check(shifted_row_three[0].reference.row == 3 && shifted_row_three[0].reference.column == 2 &&
            shifted_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_three[0].value.text_value() == "tail-b4",
        "delete_rows row_cells should keep the shifted dirty cell first");
    check(shifted_row_three[1].reference.row == 3 && shifted_row_three[1].reference.column == 3 &&
            shifted_row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_three[1].value.text_value() == "A1+B3",
        "delete_rows row_cells should keep the translated formula cell second");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_three =
        sheet.column_cells(3);
    check(shifted_column_three.size() == 1,
        "delete_rows column_cells should expose the shifted formula column snapshot");
    check(shifted_column_three[0].reference.row == 3 &&
            shifted_column_three[0].reference.column == 3 &&
            shifted_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_three[0].value.text_value() == "A1+B3",
        "delete_rows column_cells should keep the translated formula cell");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "delete_rows pre-save shift summary");
    check(sheet.has_pending_changes(),
        "delete_rows should dirty the materialized worksheet when records shift");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "delete_rows should report the dirty materialized worksheet name");
    check(editor.pending_materialized_cell_count() == 3,
        "delete_rows should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "delete_rows should report shifted sparse memory");
    check(!editor.last_edit_error().has_value(),
        "successful delete_rows should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "delete_rows save_as should project the shifted sparse dimension");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "delete_rows save_as should write the shifted source-backed cell");
    check_contains(worksheet_xml, R"(<c r="C3"><f>A1+B3</f></c>)",
        "delete_rows save_as should write the translated formula cell");
    check_contains(worksheet_xml, R"(<c r="B3")",
        "delete_rows save_as should write the shifted dirty cell");
    check_not_contains(worksheet_xml, R"(r="C4")",
        "delete_rows save_as should not keep the old formula coordinate");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "delete_rows save_as should omit deleted row text cells");
    check_not_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "delete_rows save_as should omit deleted row numeric cells");
    const auto inspect_delete_rows_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "delete_rows reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "delete_rows reopened output should expose shifted used range");
            const std::optional<fastxlsx::CellValue> reopened_a1 =
                reopened_sheet.try_cell("A1");
            check(reopened_a1.has_value() &&
                    reopened_a1->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1->text_value() == "placeholder-a2",
                "delete_rows reopened output should read shifted source A2 at A1");
            const std::optional<fastxlsx::CellValue> reopened_b3 =
                reopened_sheet.try_cell("B3");
            check(reopened_b3.has_value() &&
                    reopened_b3->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b3->text_value() == "tail-b4",
                "delete_rows reopened output should read shifted dirty text at B3");
            const std::optional<fastxlsx::CellValue> reopened_c3 =
                reopened_sheet.try_cell("C3");
            check(reopened_c3.has_value() &&
                    reopened_c3->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c3->text_value() == "A1+B3",
                "delete_rows reopened output should read translated formula at C3");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("C4").has_value() &&
                    !reopened_sheet.try_cell("B4").has_value(),
                "delete_rows reopened output should keep deleted and old coordinates absent");
        };
    check_reopened_shift_output(output, "delete_rows", inspect_delete_rows_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "delete_rows no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "delete_rows no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete_rows no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "delete_rows no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_rows no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "delete_rows no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "delete_rows no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "delete_rows no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "delete_rows no-op save",
        inspect_delete_rows_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_rows second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "delete_rows second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete_rows second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "delete_rows second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_rows second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop, "delete_rows second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop, "delete_rows second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "delete_rows second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_rows second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows second no-op save should leave the source package unchanged");
    check_reopened_shift_output(second_noop_output, "delete_rows second no-op save",
        inspect_delete_rows_output);

    sheet.set_cell("D3", fastxlsx::CellValue::text("post-noop-delete-rows-basic"));
    check(sheet.has_pending_changes(),
        "delete_rows post-noop edit should dirty the saved handle");
    check(sheet.cell_count() == 4,
        "delete_rows post-noop edit should add one sparse cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 4,
        "delete_rows post-noop edit should expand bounds to D3");
    const std::optional<fastxlsx::CellValue> retained_formula = sheet.try_cell("C3");
    check(retained_formula.has_value() &&
            retained_formula->kind() == fastxlsx::CellValueKind::Formula &&
            retained_formula->text_value() == "A1+B3",
        "delete_rows post-noop edit should preserve the shifted formula text");
    check(editor.pending_change_count() == 1,
        "delete_rows post-noop edit should retain the prior handoff before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "delete_rows post-noop edit should report the dirty materialized sheet");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_rows post-noop save should clean the materialized handle");
    check(editor.pending_change_count() == 2,
        "delete_rows post-noop save should record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_rows post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_rows post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "delete_rows post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_rows post-noop save should leave the no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows post-noop save should leave the source package unchanged");
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<dimension ref="A1:D3"/>)",
        "delete_rows post-noop save should project the expanded sparse dimension");
    check_contains(post_noop_xml, R"(<c r="A1")",
        "delete_rows post-noop save should keep the shifted source-backed cell");
    check_contains(post_noop_xml, R"(<c r="B3")",
        "delete_rows post-noop save should keep the shifted dirty cell");
    check_contains(post_noop_xml, R"(<c r="C3"><f>A1+B3</f></c>)",
        "delete_rows post-noop save should keep the translated formula cell");
    check_contains(post_noop_xml, R"(<c r="D3")",
        "delete_rows post-noop save should write the post-noop edit");
    check_contains(post_noop_xml, "post-noop-delete-rows-basic",
        "delete_rows post-noop save should write the post-noop edit text");
    check_not_contains(post_noop_xml, R"(r="C4")",
        "delete_rows post-noop save should not resurrect the old formula coordinate");
    check_not_contains(post_noop_xml, R"(r="B4")",
        "delete_rows post-noop save should not resurrect the old dirty coordinate");

    const auto inspect_delete_rows_post_noop_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "delete_rows post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 4,
                "delete_rows post-noop reopened output should expose expanded bounds");
            const std::optional<fastxlsx::CellValue> reopened_a1 =
                reopened_sheet.try_cell("A1");
            check(reopened_a1.has_value() &&
                    reopened_a1->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1->text_value() == "placeholder-a2",
                "delete_rows post-noop reopened output should read shifted source A2 at A1");
            const std::optional<fastxlsx::CellValue> reopened_b3 =
                reopened_sheet.try_cell("B3");
            check(reopened_b3.has_value() &&
                    reopened_b3->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b3->text_value() == "tail-b4",
                "delete_rows post-noop reopened output should read shifted dirty text at B3");
            const std::optional<fastxlsx::CellValue> reopened_c3 =
                reopened_sheet.try_cell("C3");
            check(reopened_c3.has_value() &&
                    reopened_c3->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c3->text_value() == "A1+B3",
                "delete_rows post-noop reopened output should read translated formula at C3");
            const std::optional<fastxlsx::CellValue> reopened_d3 =
                reopened_sheet.try_cell("D3");
            check(reopened_d3.has_value() &&
                    reopened_d3->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d3->text_value() == "post-noop-delete-rows-basic",
                "delete_rows post-noop reopened output should read the post-noop edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("C4").has_value() &&
                    !reopened_sheet.try_cell("B4").has_value(),
                "delete_rows post-noop reopened output should keep deleted and old coordinates absent");
        };
    check_reopened_shift_output(post_noop_output,
        "delete_rows post-noop save",
        inspect_delete_rows_post_noop_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(post_noop_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_rows post-noop noop save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "delete_rows post-noop noop save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_rows post-noop noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_rows post-noop noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows post-noop noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_post_noop_noop,
        "delete_rows post-noop noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_post_noop_noop,
        "delete_rows post-noop noop save");
    const auto post_noop_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_noop_output);
    check(post_noop_noop_entries == post_noop_entries,
        "delete_rows post-noop noop output should match post-noop output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows post-noop noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
        "delete_rows post-noop noop save should leave prior post-noop output unchanged");
    check_reopened_shift_output(post_noop_noop_output,
        "delete_rows post-noop noop save",
        inspect_delete_rows_post_noop_output);
}

void test_public_worksheet_editor_insert_columns_shifts_sparse_records()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-insert-columns-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-post-noop-output.xlsx");
    const std::filesystem::path post_noop_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-post-noop-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(2, 3, fastxlsx::CellValue::formula("A1+B1"));
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("extra-c3"));
    sheet.insert_columns(2, 2);

    check(sheet.cell_count() == 5,
        "insert_columns should preserve sparse cell count when it only shifts records");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "insert_columns should preserve cells left of the insertion point");
    check(sheet.get_cell("A2").text_value() == "placeholder-a2",
        "insert_columns should preserve lower cells left of the insertion point");
    check(sheet.get_cell("D1").number_value() == 1.0,
        "insert_columns should shift source-backed cells right by column_count");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("E2");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula
            && shifted_formula.text_value() == "A1+D1",
        "insert_columns should translate moved formula text by the column shift");
    check(sheet.get_cell("E3").text_value() == "extra-c3",
        "insert_columns should shift dirty cells right by column_count");
    check(!sheet.try_cell("B1").has_value(),
        "insert_columns should leave inserted sparse columns without synthesized cells");
    check(!sheet.try_cell("C2").has_value(),
        "insert_columns should remove the old shifted formula coordinate");
    check(!sheet.try_cell("C3").has_value(),
        "insert_columns should remove the old shifted dirty coordinate");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 5,
        "insert_columns should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_two = sheet.row_cells(2);
    check(shifted_row_two.size() == 2,
        "insert_columns row_cells should expose the shifted row snapshot");
    check(shifted_row_two[0].reference.row == 2 && shifted_row_two[0].reference.column == 1 &&
            shifted_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_two[0].value.text_value() == "placeholder-a2",
        "insert_columns row_cells should keep the source-backed cell first");
    check(shifted_row_two[1].reference.row == 2 && shifted_row_two[1].reference.column == 5 &&
            shifted_row_two[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_two[1].value.text_value() == "A1+D1",
        "insert_columns row_cells should keep the translated formula cell second");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_five =
        sheet.column_cells(5);
    check(shifted_column_five.size() == 2,
        "insert_columns column_cells should expose the shifted formula column snapshot");
    check(shifted_column_five[0].reference.row == 2 &&
            shifted_column_five[0].reference.column == 5 &&
            shifted_column_five[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_five[0].value.text_value() == "A1+D1",
        "insert_columns column_cells should keep the translated formula cell first");
    check(shifted_column_five[1].reference.row == 3 &&
            shifted_column_five[1].reference.column == 5 &&
            shifted_column_five[1].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_column_five[1].value.text_value() == "extra-c3",
        "insert_columns column_cells should keep the shifted dirty cell second");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "insert_columns pre-save shift summary");
    check(sheet.has_pending_changes(),
        "insert_columns should dirty the materialized worksheet when records shift");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "insert_columns should report the dirty materialized worksheet name");
    check(editor.pending_materialized_cell_count() == 5,
        "insert_columns should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "insert_columns should report shifted sparse memory");
    check(!editor.last_edit_error().has_value(),
        "successful insert_columns should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E3"/>)",
        "insert_columns save_as should project the shifted sparse dimension");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "insert_columns save_as should write the shifted source-backed numeric cell");
    check_contains(worksheet_xml, R"(<c r="E2"><f>A1+D1</f></c>)",
        "insert_columns save_as should write the translated formula cell");
    check_contains(worksheet_xml, R"(<c r="E3")",
        "insert_columns save_as should write the shifted dirty cell");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "insert_columns save_as should not keep the old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "insert_columns save_as should not keep the old shifted source coordinate");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "insert_columns save_as should not keep the old shifted dirty coordinate");
    const auto inspect_insert_columns_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "insert_columns reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 5,
                "insert_columns reopened output should expose shifted used range");
            const std::optional<fastxlsx::CellValue> reopened_a1 =
                reopened_sheet.try_cell("A1");
            check(reopened_a1.has_value() &&
                    reopened_a1->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1->text_value() == "placeholder-a1",
                "insert_columns reopened output should keep A1 left of insertion point");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1->number_value() == 1.0,
                "insert_columns reopened output should read shifted source B1 at D1");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_e2.has_value() &&
                    reopened_e2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_e2->text_value() == "A1+D1",
                "insert_columns reopened output should read translated formula at E2");
            const std::optional<fastxlsx::CellValue> reopened_e3 =
                reopened_sheet.try_cell("E3");
            check(reopened_e3.has_value() &&
                    reopened_e3->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e3->text_value() == "extra-c3",
                "insert_columns reopened output should read shifted dirty cell at E3");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "insert_columns reopened output should keep old sparse coordinates absent");
        };
    check_reopened_shift_output(output, "insert_columns", inspect_insert_columns_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "insert_columns no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "insert_columns no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "insert_columns no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "insert_columns no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_columns no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "insert_columns no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "insert_columns no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "insert_columns no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "insert_columns no-op save",
        inspect_insert_columns_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "insert_columns second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "insert_columns second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "insert_columns second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "insert_columns second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_columns second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop, "insert_columns second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop, "insert_columns second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "insert_columns second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_columns second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns second no-op save should leave the source package unchanged");
    check_reopened_shift_output(second_noop_output, "insert_columns second no-op save",
        inspect_insert_columns_output);

    sheet.set_cell("F3", fastxlsx::CellValue::text("post-noop-insert-columns-basic"));
    check(sheet.has_pending_changes(),
        "insert_columns post-noop edit should dirty the saved handle");
    check(sheet.cell_count() == 6,
        "insert_columns post-noop edit should add one sparse cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 6,
        "insert_columns post-noop edit should expand bounds to F3");
    const std::optional<fastxlsx::CellValue> retained_formula = sheet.try_cell("E2");
    check(retained_formula.has_value() &&
            retained_formula->kind() == fastxlsx::CellValueKind::Formula &&
            retained_formula->text_value() == "A1+D1",
        "insert_columns post-noop edit should preserve the shifted formula text");
    check(editor.pending_change_count() == 1,
        "insert_columns post-noop edit should retain the prior handoff before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 6 &&
            editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "insert_columns post-noop edit should report the dirty materialized sheet");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "insert_columns post-noop save should clean the materialized handle");
    check(editor.pending_change_count() == 2,
        "insert_columns post-noop save should record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_columns post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "insert_columns post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_columns post-noop save should leave the no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns post-noop save should leave the source package unchanged");
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<dimension ref="A1:F3"/>)",
        "insert_columns post-noop save should project the expanded sparse dimension");
    check_contains(post_noop_xml, R"(<c r="D1"><v>1</v></c>)",
        "insert_columns post-noop save should keep the shifted source-backed numeric cell");
    check_contains(post_noop_xml, R"(<c r="E2"><f>A1+D1</f></c>)",
        "insert_columns post-noop save should keep the translated formula cell");
    check_contains(post_noop_xml, R"(<c r="E3")",
        "insert_columns post-noop save should keep the shifted dirty cell");
    check_contains(post_noop_xml, R"(<c r="F3")",
        "insert_columns post-noop save should write the post-noop edit");
    check_contains(post_noop_xml, "post-noop-insert-columns-basic",
        "insert_columns post-noop save should write the post-noop edit text");
    check_not_contains(post_noop_xml, R"(r="B1")",
        "insert_columns post-noop save should not resurrect the old shifted source coordinate");
    check_not_contains(post_noop_xml, R"(r="C2")",
        "insert_columns post-noop save should not resurrect the old formula coordinate");
    check_not_contains(post_noop_xml, R"(r="C3")",
        "insert_columns post-noop save should not resurrect the old dirty coordinate");

    const auto inspect_insert_columns_post_noop_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "insert_columns post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "insert_columns post-noop reopened output should expose expanded bounds");
            const std::optional<fastxlsx::CellValue> reopened_a1 =
                reopened_sheet.try_cell("A1");
            check(reopened_a1.has_value() &&
                    reopened_a1->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1->text_value() == "placeholder-a1",
                "insert_columns post-noop reopened output should keep A1 left of insertion point");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1->number_value() == 1.0,
                "insert_columns post-noop reopened output should read shifted source B1 at D1");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_e2.has_value() &&
                    reopened_e2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_e2->text_value() == "A1+D1",
                "insert_columns post-noop reopened output should read translated formula at E2");
            const std::optional<fastxlsx::CellValue> reopened_e3 =
                reopened_sheet.try_cell("E3");
            check(reopened_e3.has_value() &&
                    reopened_e3->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e3->text_value() == "extra-c3",
                "insert_columns post-noop reopened output should read shifted dirty cell at E3");
            const std::optional<fastxlsx::CellValue> reopened_f3 =
                reopened_sheet.try_cell("F3");
            check(reopened_f3.has_value() &&
                    reopened_f3->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_f3->text_value() == "post-noop-insert-columns-basic",
                "insert_columns post-noop reopened output should read the post-noop edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "insert_columns post-noop reopened output should keep old sparse coordinates absent");
        };
    check_reopened_shift_output(post_noop_output,
        "insert_columns post-noop save",
        inspect_insert_columns_post_noop_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(post_noop_noop_output);
    check(!sheet.has_pending_changes(),
        "insert_columns post-noop noop save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "insert_columns post-noop noop save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns post-noop noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_columns post-noop noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns post-noop noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_post_noop_noop,
        "insert_columns post-noop noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_post_noop_noop,
        "insert_columns post-noop noop save");
    const auto post_noop_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_noop_output);
    check(post_noop_noop_entries == post_noop_entries,
        "insert_columns post-noop noop output should match post-noop output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns post-noop noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
        "insert_columns post-noop noop save should leave prior post-noop output unchanged");
    check_reopened_shift_output(post_noop_noop_output,
        "insert_columns post-noop noop save",
        inspect_insert_columns_post_noop_output);
}

void test_public_worksheet_editor_insert_columns_preserves_shifted_source_formula_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-insert-columns-styled-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-styled-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-styled-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-styled-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-styled-post-noop-output.xlsx");
    const std::filesystem::path post_noop_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-styled-post-noop-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_columns(2, 2);

    check(sheet.cell_count() == 7,
        "insert_columns styled source formula should preserve shifted sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 6,
        "insert_columns styled source formula should expose shifted bounds");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
            sheet.get_cell("A2").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "extra-c3",
        "insert_columns styled source formula should preserve cells left of the insertion point");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("D1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "insert_columns styled source formula should shift B1 to D1");
    check(sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
            sheet.get_cell("E2").text_value() == "row2-gap-c2",
        "insert_columns styled source formula should shift row-two filler cells");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("F2");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+D1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "insert_columns styled source formula should translate moved formula and preserve style id");
    check(!sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("B2").has_value() &&
            !sheet.try_cell("C2").has_value(),
        "insert_columns styled source formula should leave inserted sparse columns empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_two = sheet.row_cells(2);
    check(shifted_row_two.size() == 4,
        "insert_columns styled source formula row_cells should expose shifted row two");
    check(shifted_row_two[0].reference.row == 2 &&
            shifted_row_two[0].reference.column == 1 &&
            shifted_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_two[0].value.text_value() == "placeholder-a2",
        "insert_columns styled source formula row_cells should keep the source-backed cell first");
    check(shifted_row_two[3].reference.row == 2 &&
            shifted_row_two[3].reference.column == 6 &&
            shifted_row_two[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_two[3].value.text_value() == "A1+D1" &&
            shifted_row_two[3].value.has_style() &&
            shifted_row_two[3].value.style_id().value() == styled_formula_style.value(),
        "insert_columns styled source formula row_cells should keep shifted formula style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_six =
        sheet.column_cells(6);
    check(shifted_column_six.size() == 1,
        "insert_columns styled source formula column_cells should expose shifted formula column");
    check(shifted_column_six[0].reference.row == 2 &&
            shifted_column_six[0].reference.column == 6 &&
            shifted_column_six[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_six[0].value.text_value() == "A1+D1" &&
            shifted_column_six[0].value.has_style() &&
            shifted_column_six[0].value.style_id().value() == styled_formula_style.value(),
        "insert_columns styled source formula column_cells should keep shifted formula style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 7 &&
            editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "insert_columns styled source formula should report shifted dirty materialized state");
    check(!editor.last_edit_error().has_value(),
        "insert_columns styled source formula should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns styled source formula save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="F2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+D1</f></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:F3"/>)",
        "insert_columns styled source formula save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "insert_columns styled source formula save_as should write shifted B1");
    check_contains(worksheet_xml, R"(<c r="D2")",
        "insert_columns styled source formula save_as should write shifted B2");
    check_contains(worksheet_xml, R"(<c r="E2")",
        "insert_columns styled source formula save_as should write shifted C2");
    check_contains(worksheet_xml, styled_formula_xml,
        "insert_columns styled source formula save_as should write shifted formula with style id");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "insert_columns styled source formula save_as should omit inserted B1");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "insert_columns styled source formula save_as should omit inserted C2");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "insert_columns styled source formula should preserve untouched worksheets");

    const auto inspect_insert_columns_styled_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "insert_columns styled source formula reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "insert_columns styled source formula reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1->number_value() == 1.0,
                "insert_columns styled source formula reopened output should read shifted B1");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_f2->text_value() == "A1+D1" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "insert_columns styled source formula reopened output should read styled formula");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("A2").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("E2").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A3").text_value() == "extra-c3",
                "insert_columns styled source formula reopened output should read shifted source cells");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "insert_columns styled source formula reopened output should keep inserted coordinates absent");
        };
    check_reopened_shift_output(output, "insert_columns styled source formula",
        inspect_insert_columns_styled_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "insert_columns styled source formula no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "insert_columns styled source formula no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns styled source formula no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_columns styled source formula no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns styled source formula no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "insert_columns styled source formula no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "insert_columns styled source formula no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "insert_columns styled source formula no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns styled source formula no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output,
        "insert_columns styled source formula no-op save",
        inspect_insert_columns_styled_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "insert_columns styled source formula second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "insert_columns styled source formula second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns styled source formula second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_columns styled source formula second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns styled source formula second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "insert_columns styled source formula second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "insert_columns styled source formula second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "insert_columns styled source formula second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns styled source formula second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_columns styled source formula second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "insert_columns styled source formula second no-op save",
        inspect_insert_columns_styled_output);

    sheet.set_cell("G2", fastxlsx::CellValue::text("post-noop-insert-columns-styled"));
    check(sheet.has_pending_changes(),
        "insert_columns styled source formula post-noop edit should dirty the saved handle");
    check(sheet.cell_count() == 8,
        "insert_columns styled source formula post-noop edit should add one sparse cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 7,
        "insert_columns styled source formula post-noop edit should expand bounds to G2");
    const std::optional<fastxlsx::CellValue> retained_formula = sheet.try_cell("F2");
    check(retained_formula.has_value() &&
            retained_formula->kind() == fastxlsx::CellValueKind::Formula &&
            retained_formula->text_value() == "A1+D1" &&
            retained_formula->has_style() &&
            retained_formula->style_id().value() == styled_formula_style.value(),
        "insert_columns styled source formula post-noop edit should preserve shifted formula style id");
    check(editor.pending_change_count() == 1,
        "insert_columns styled source formula post-noop edit should retain the prior handoff before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 8 &&
            editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "insert_columns styled source formula post-noop edit should report the dirty materialized sheet");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "insert_columns styled source formula post-noop save should clean the materialized handle");
    check(editor.pending_change_count() == 2,
        "insert_columns styled source formula post-noop save should record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns styled source formula post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_columns styled source formula post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns styled source formula post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "insert_columns styled source formula post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_columns styled source formula post-noop save should leave the no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "insert_columns styled source formula post-noop save should leave the second no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns styled source formula post-noop save should leave the source package unchanged");
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, styled_formula_xml,
        "insert_columns styled source formula post-noop save should keep the styled formula cell");
    check_contains(post_noop_xml, R"(<c r="G2")",
        "insert_columns styled source formula post-noop save should write the post-noop edit");
    const auto inspect_insert_columns_styled_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "insert_columns styled source formula post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 7,
                "insert_columns styled source formula post-noop reopened output should expose expanded bounds");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_f2->text_value() == "A1+D1" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "insert_columns styled source formula post-noop reopened output should keep styled formula");
            const std::optional<fastxlsx::CellValue> reopened_g2 =
                reopened_sheet.try_cell("G2");
            check(reopened_g2.has_value() &&
                    reopened_g2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_g2->text_value() == "post-noop-insert-columns-styled",
                "insert_columns styled source formula post-noop reopened output should read post-noop edit");
            check(reopened_sheet.get_cell("D1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("E2").text_value() == "row2-gap-c2",
                "insert_columns styled source formula post-noop reopened output should keep shifted source cells");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "insert_columns styled source formula post-noop reopened output should keep inserted coordinates absent");
        };
    check_reopened_shift_output(post_noop_output,
        "insert_columns styled source formula post-noop save",
        inspect_insert_columns_styled_post_noop_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(post_noop_noop_output);
    check(!sheet.has_pending_changes(),
        "insert_columns styled source formula post-noop noop save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "insert_columns styled source formula post-noop noop save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns styled source formula post-noop noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "insert_columns styled source formula post-noop noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns styled source formula post-noop noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_post_noop_noop,
        "insert_columns styled source formula post-noop noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_post_noop_noop,
        "insert_columns styled source formula post-noop noop save");
    const auto post_noop_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_noop_output);
    check(post_noop_noop_entries == post_noop_entries,
        "insert_columns styled source formula post-noop noop output should match post-noop output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns styled source formula post-noop noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
        "insert_columns styled source formula post-noop noop save should leave prior post-noop output unchanged");
    check_reopened_shift_output(post_noop_noop_output,
        "insert_columns styled source formula post-noop noop save",
        inspect_insert_columns_styled_post_noop_output);
}

void test_public_worksheet_editor_insert_columns_preserves_shifted_value_only_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-insert-columns-value-only-style-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-value-only-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-value-only-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-value-only-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-value-only-style-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell_value("D2", fastxlsx::CellValue::text("column-value-only-styled"));
    const std::optional<fastxlsx::CellValue> value_only_d2 = sheet.try_cell("D2");
    check(value_only_d2.has_value() &&
            value_only_d2->kind() == fastxlsx::CellValueKind::Text &&
            value_only_d2->text_value() == "column-value-only-styled" &&
            value_only_d2->has_style() &&
            value_only_d2->style_id().value() == styled_formula_style.value(),
        "set_cell_value should preserve the source style before insert_columns shifts it");

    sheet.insert_columns(2, 2);

    check(sheet.cell_count() == 7,
        "insert_columns should keep value-only shifted sparse count stable");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 6,
        "insert_columns should refresh bounds for shifted value-only styled cells");
    const std::optional<fastxlsx::CellValue> shifted_value = sheet.try_cell("F2");
    check(shifted_value.has_value() &&
            shifted_value->kind() == fastxlsx::CellValueKind::Text &&
            shifted_value->text_value() == "column-value-only-styled" &&
            shifted_value->has_style() &&
            shifted_value->style_id().value() == styled_formula_style.value(),
        "insert_columns should move value-only cells with the preserved source style id");
    check(!sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("B2").has_value() &&
            !sheet.try_cell("C2").has_value(),
        "insert_columns should leave inserted sparse columns empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_two = sheet.row_cells(2);
    check(shifted_row_two.size() == 4,
        "insert_columns value-only row_cells should expose the shifted source row");
    check(shifted_row_two[3].reference.row == 2 &&
            shifted_row_two[3].reference.column == 6 &&
            shifted_row_two[3].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_two[3].value.text_value() == "column-value-only-styled" &&
            shifted_row_two[3].value.has_style() &&
            shifted_row_two[3].value.style_id().value() == styled_formula_style.value(),
        "insert_columns value-only row_cells should keep the shifted style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_six =
        sheet.column_cells(6);
    check(shifted_column_six.size() == 1 &&
            shifted_column_six[0].reference.row == 2 &&
            shifted_column_six[0].reference.column == 6 &&
            shifted_column_six[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_column_six[0].value.text_value() == "column-value-only-styled" &&
            shifted_column_six[0].value.has_style() &&
            shifted_column_six[0].value.style_id().value() == styled_formula_style.value(),
        "insert_columns value-only column_cells should keep the shifted style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "insert_columns value-only style pre-save shift summary");
    check(editor.pending_materialized_cell_count() == 7,
        "insert_columns value-only style should keep aggregate materialized cell count stable");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "insert_columns value-only style should keep aggregate materialized memory stable");
    check(!editor.last_edit_error().has_value(),
        "successful insert_columns value-only style shift should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns value-only style save_as should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "insert_columns value-only style save_as should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_text =
        R"(<c r="F2" s=")" + std::to_string(styled_formula_style.value())
        + R"(" t="inlineStr"><is><t>column-value-only-styled</t></is></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:F3"/>)",
        "insert_columns value-only style save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_text,
        "insert_columns value-only style save_as should write shifted text with source style id");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "insert_columns value-only style save_as should keep shifted source number");
    check_contains(worksheet_xml, R"(<c r="D2" t="inlineStr"><is><t>row2-gap-b2</t></is></c>)",
        "insert_columns value-only style save_as should keep shifted source B2");
    check_contains(worksheet_xml, R"(<c r="E2" t="inlineStr"><is><t>row2-gap-c2</t></is></c>)",
        "insert_columns value-only style save_as should keep shifted source C2");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "insert_columns value-only style save_as should omit inserted B1");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "insert_columns value-only style save_as should omit inserted C2");
    check_not_contains(worksheet_xml, R"(<f>A1+B1</f>)",
        "insert_columns value-only style save_as should omit the source formula");
    check_not_contains(worksheet_xml, R"(<f>A1+D1</f>)",
        "insert_columns value-only style save_as should not resurrect the shifted formula");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "insert_columns value-only style should preserve untouched worksheets");
    const auto inspect_value_only_shift_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "insert_columns value-only style reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "insert_columns value-only style reopened output should keep shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_f2->text_value() == "column-value-only-styled" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "insert_columns value-only style reopened output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1->number_value() == 1.0,
                "insert_columns value-only style reopened output should keep shifted source number");
            const std::optional<fastxlsx::CellValue> reopened_d2 =
                reopened_sheet.try_cell("D2");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_d2.has_value() &&
                    reopened_d2->text_value() == "row2-gap-b2" &&
                    reopened_e2.has_value() &&
                    reopened_e2->text_value() == "row2-gap-c2",
                "insert_columns value-only style reopened output should keep shifted source cells");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "insert_columns value-only style reopened output should keep inserted coordinates absent");
        };
    check_reopened_shift_output(output, "insert_columns value-only style",
        inspect_value_only_shift_output);
    check_reopened_untouched_keep_me_output(output, "insert_columns value-only style");
    const auto noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        "insert_columns value-only style no-op save",
        inspect_value_only_shift_output);
    const auto second_noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        "insert_columns value-only style second no-op save",
        inspect_value_only_shift_output);
    check(second_noop_entries == noop_entries,
        "insert_columns value-only style second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_columns value-only style second no-op save should leave first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "insert_columns value-only style second no-op save should leave the materialized output unchanged");
    const auto inspect_value_only_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "insert_columns value-only style post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 7,
                "insert_columns value-only style post-noop output should expand to the later edit");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_f2->text_value() == "column-value-only-styled" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "insert_columns value-only style post-noop output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_g2 =
                reopened_sheet.try_cell("G2");
            check(reopened_g2.has_value() &&
                    reopened_g2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_g2->text_value() == "post-noop-insert-columns-value-only-style",
                "insert_columns value-only style post-noop output should include the later edit");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            const std::optional<fastxlsx::CellValue> reopened_d2 =
                reopened_sheet.try_cell("D2");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1->number_value() == 1.0 &&
                    reopened_d2.has_value() &&
                    reopened_d2->text_value() == "row2-gap-b2" &&
                    reopened_e2.has_value() &&
                    reopened_e2->text_value() == "row2-gap-c2",
                "insert_columns value-only style post-noop output should keep shifted source cells");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "insert_columns value-only style post-noop output should keep inserted coordinates absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "insert_columns value-only style post-noop edit save",
        8,
        [&sheet]() {
            sheet.set_cell("G2",
                fastxlsx::CellValue::text("post-noop-insert-columns-value-only-style"));
        },
        inspect_value_only_post_noop_output);
}

void test_public_worksheet_editor_insert_columns_preserves_shifted_clear_value_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-insert-columns-clear-value-style-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-clear-value-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-clear-value-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-clear-value-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-columns-clear-value-style-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.clear_cell_value("D2");
    const std::optional<fastxlsx::CellValue> cleared_d2 = sheet.try_cell("D2");
    check(cleared_d2.has_value() &&
            cleared_d2->kind() == fastxlsx::CellValueKind::Blank &&
            cleared_d2->has_style() &&
            cleared_d2->style_id().value() == styled_formula_style.value(),
        "clear_cell_value should preserve the source style before insert_columns shifts it");

    sheet.insert_columns(2, 2);

    check(sheet.cell_count() == 7,
        "insert_columns should keep cleared shifted sparse count stable");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 6,
        "insert_columns should refresh bounds for shifted cleared styled cells");
    const std::optional<fastxlsx::CellValue> shifted_blank = sheet.try_cell("F2");
    check(shifted_blank.has_value() &&
            shifted_blank->kind() == fastxlsx::CellValueKind::Blank &&
            shifted_blank->has_style() &&
            shifted_blank->style_id().value() == styled_formula_style.value(),
        "insert_columns should move cleared cells with the preserved source style id");
    check(!sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("B2").has_value() &&
            !sheet.try_cell("C2").has_value(),
        "insert_columns should leave inserted sparse columns empty for cleared shifts");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_two = sheet.row_cells(2);
    check(shifted_row_two.size() == 4,
        "insert_columns cleared row_cells should expose the shifted source row");
    check(shifted_row_two[3].reference.row == 2 &&
            shifted_row_two[3].reference.column == 6 &&
            shifted_row_two[3].value.kind() == fastxlsx::CellValueKind::Blank &&
            shifted_row_two[3].value.has_style() &&
            shifted_row_two[3].value.style_id().value() == styled_formula_style.value(),
        "insert_columns cleared row_cells should keep the shifted style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_six =
        sheet.column_cells(6);
    check(shifted_column_six.size() == 1 &&
            shifted_column_six[0].reference.row == 2 &&
            shifted_column_six[0].reference.column == 6 &&
            shifted_column_six[0].value.kind() == fastxlsx::CellValueKind::Blank &&
            shifted_column_six[0].value.has_style() &&
            shifted_column_six[0].value.style_id().value() == styled_formula_style.value(),
        "insert_columns cleared column_cells should keep the shifted style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "insert_columns cleared style pre-save shift summary");
    check(editor.pending_materialized_cell_count() == 7,
        "insert_columns cleared style should keep aggregate materialized cell count stable");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "insert_columns cleared style should keep aggregate materialized memory stable");
    check(!editor.last_edit_error().has_value(),
        "successful insert_columns cleared style shift should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns cleared style save_as should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "insert_columns cleared style save_as should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_blank =
        R"(<c r="F2" s=")" + std::to_string(styled_formula_style.value()) + R"("/>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:F3"/>)",
        "insert_columns cleared style save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_blank,
        "insert_columns cleared style save_as should write shifted blank with source style id");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "insert_columns cleared style save_as should keep shifted source number");
    check_contains(worksheet_xml, R"(<c r="D2" t="inlineStr"><is><t>row2-gap-b2</t></is></c>)",
        "insert_columns cleared style save_as should keep shifted source B2");
    check_contains(worksheet_xml, R"(<c r="E2" t="inlineStr"><is><t>row2-gap-c2</t></is></c>)",
        "insert_columns cleared style save_as should keep shifted source C2");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "insert_columns cleared style save_as should omit inserted B1");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "insert_columns cleared style save_as should omit inserted C2");
    check_not_contains(worksheet_xml, R"(<f>A1+B1</f>)",
        "insert_columns cleared style save_as should omit the cleared source formula");
    check_not_contains(worksheet_xml, R"(<f>A1+D1</f>)",
        "insert_columns cleared style save_as should not resurrect the shifted formula");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "insert_columns cleared style should preserve untouched worksheets");
    const auto inspect_cleared_shift_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "insert_columns cleared style reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "insert_columns cleared style reopened output should keep shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Blank &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "insert_columns cleared style reopened output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1->number_value() == 1.0,
                "insert_columns cleared style reopened output should keep shifted source number");
            const std::optional<fastxlsx::CellValue> reopened_d2 =
                reopened_sheet.try_cell("D2");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_d2.has_value() &&
                    reopened_d2->text_value() == "row2-gap-b2" &&
                    reopened_e2.has_value() &&
                    reopened_e2->text_value() == "row2-gap-c2",
                "insert_columns cleared style reopened output should keep shifted source cells");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "insert_columns cleared style reopened output should keep inserted coordinates absent");
        };
    check_reopened_shift_output(output, "insert_columns cleared style",
        inspect_cleared_shift_output);
    check_reopened_untouched_keep_me_output(output, "insert_columns cleared style");
    const auto noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        "insert_columns cleared style no-op save",
        inspect_cleared_shift_output);
    const auto second_noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        "insert_columns cleared style second no-op save",
        inspect_cleared_shift_output);
    check(second_noop_entries == noop_entries,
        "insert_columns cleared style second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_columns cleared style second no-op save should leave first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "insert_columns cleared style second no-op save should leave the materialized output unchanged");
    const auto inspect_cleared_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "insert_columns cleared style post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 7,
                "insert_columns cleared style post-noop output should expand to the later edit");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Blank &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "insert_columns cleared style post-noop output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_g2 =
                reopened_sheet.try_cell("G2");
            check(reopened_g2.has_value() &&
                    reopened_g2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_g2->text_value() == "post-noop-insert-columns-cleared-style",
                "insert_columns cleared style post-noop output should include the later edit");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            const std::optional<fastxlsx::CellValue> reopened_d2 =
                reopened_sheet.try_cell("D2");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1->number_value() == 1.0 &&
                    reopened_d2.has_value() &&
                    reopened_d2->text_value() == "row2-gap-b2" &&
                    reopened_e2.has_value() &&
                    reopened_e2->text_value() == "row2-gap-c2",
                "insert_columns cleared style post-noop output should keep shifted source cells");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "insert_columns cleared style post-noop output should keep inserted coordinates absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "insert_columns cleared style post-noop edit save",
        8,
        [&sheet]() {
            sheet.set_cell("G2",
                fastxlsx::CellValue::text("post-noop-insert-columns-cleared-style"));
        },
        inspect_cleared_post_noop_output);
}

void test_public_worksheet_editor_full_calculation_preserves_insert_columns_styled_formula_shift()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-columns-styled-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-columns-styled-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-columns-styled-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-columns-styled-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-columns-styled-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_columns(2, 2);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 7,
        "full-calc insert_columns styled formula setup should keep shifted sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 6,
        "full-calc insert_columns styled formula setup should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("F2");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+D1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "full-calc insert_columns styled formula setup should translate formula and preserve style id");
    check(editor.pending_change_count() == 0,
        "full-calc insert_columns styled formula setup should not queue a Patch handoff before save_as");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "full-calc insert_columns styled formula setup should report Data dirty");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "full-calc insert_columns styled formula setup should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "full-calc insert_columns styled formula setup should report shifted sparse memory");

    editor.request_full_calculation();

    check(!editor.last_edit_error().has_value(),
        "request_full_calculation after insert_columns styled formula should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "request_full_calculation after insert_columns styled formula should add one metadata edit");
    check(sheet.has_pending_changes(),
        "request_full_calculation after insert_columns styled formula should keep the shifted sheet dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "request_full_calculation after insert_columns styled formula should preserve dirty names");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "request_full_calculation after insert_columns styled formula should preserve dirty sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "request_full_calculation after insert_columns styled formula should preserve dirty sparse memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 1,
        "request_full_calculation after insert_columns styled formula dirty summary");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc insert_columns styled formula save_as should clean the shifted materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc insert_columns styled formula save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc insert_columns styled formula save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc insert_columns styled formula save_as should clear dirty materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc insert_columns styled formula save_as should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc insert_columns styled formula save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="F2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+D1</f></c>)";
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "full-calc insert_columns styled formula save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc insert_columns styled formula save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:F3"/>)",
        "full-calc insert_columns styled formula save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "full-calc insert_columns styled formula save_as should write shifted source-backed number");
    check_contains(worksheet_xml, R"(<c r="D2")",
        "full-calc insert_columns styled formula save_as should write shifted B2");
    check_contains(worksheet_xml, R"(<c r="E2")",
        "full-calc insert_columns styled formula save_as should write shifted C2");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc insert_columns styled formula save_as should write shifted styled formula");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "full-calc insert_columns styled formula save_as should omit inserted B1");
    check_not_contains(worksheet_xml, R"(r="B2")",
        "full-calc insert_columns styled formula save_as should omit inserted B2");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "full-calc insert_columns styled formula save_as should omit inserted C2");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc insert_columns styled formula should preserve untouched worksheets");

    const auto inspect_full_calc_insert_columns_styled_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc insert_columns styled formula reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "full-calc insert_columns styled formula reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1->number_value() == 1.0,
                "full-calc insert_columns styled formula reopened output should read shifted number");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_f2->text_value() == "A1+D1" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "full-calc insert_columns styled formula reopened output should read styled formula");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("A2").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("A3").text_value() == "extra-c3" &&
                    reopened_sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("E2").text_value() == "row2-gap-c2",
                "full-calc insert_columns styled formula reopened output should read shifted source cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 4 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 4 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_two[2].reference.row == 2 &&
                    reopened_row_two[2].reference.column == 5 &&
                    reopened_row_two[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_two[3].reference.row == 2 &&
                    reopened_row_two[3].reference.column == 6 &&
                    reopened_row_two[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_two[3].value.text_value() == "A1+D1" &&
                    reopened_row_two[3].value.has_style() &&
                    reopened_row_two[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc insert_columns styled formula reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_six =
                reopened_sheet.column_cells(6);
            check(reopened_column_six.size() == 1 &&
                    reopened_column_six[0].reference.row == 2 &&
                    reopened_column_six[0].reference.column == 6 &&
                    reopened_column_six[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_six[0].value.text_value() == "A1+D1" &&
                    reopened_column_six[0].value.has_style() &&
                    reopened_column_six[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc insert_columns styled formula reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "full-calc insert_columns styled formula reopened output should keep inserted coordinates absent");
        };
    check_reopened_shift_output(output, "full-calc insert_columns styled formula",
        inspect_full_calc_insert_columns_styled_output);
    check_reopened_untouched_keep_me_output(
        output, "full-calc insert_columns styled formula Untouched");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc insert_columns styled formula no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc insert_columns styled formula no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc insert_columns styled formula no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc insert_columns styled formula no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_columns styled formula no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "full-calc insert_columns styled formula no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "full-calc insert_columns styled formula no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc insert_columns styled formula no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc insert_columns styled formula no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output,
        "full-calc insert_columns styled formula no-op save",
        inspect_full_calc_insert_columns_styled_output);
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc insert_columns styled formula no-op Untouched");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc insert_columns styled formula second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc insert_columns styled formula second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc insert_columns styled formula second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc insert_columns styled formula second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_columns styled formula second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "full-calc insert_columns styled formula second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "full-calc insert_columns styled formula second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc insert_columns styled formula second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc insert_columns styled formula second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc insert_columns styled formula second no-op save should leave materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc insert_columns styled formula second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "full-calc insert_columns styled formula second no-op save",
        inspect_full_calc_insert_columns_styled_output);
    check_reopened_untouched_keep_me_output(
        second_noop_output,
        "full-calc insert_columns styled formula second no-op Untouched");
    const auto inspect_full_calc_insert_columns_styled_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 8,
                "full-calc insert_columns styled formula post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 7,
                "full-calc insert_columns styled formula post-noop output should expand to the later edit");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_f2->text_value() == "A1+D1" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "full-calc insert_columns styled formula post-noop output should keep the shifted styled formula");
            const std::optional<fastxlsx::CellValue> reopened_g2 =
                reopened_sheet.try_cell("G2");
            check(reopened_g2.has_value() &&
                    reopened_g2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_g2->text_value() == "post-noop-full-calc-insert-columns",
                "full-calc insert_columns styled formula post-noop output should include the later edit");
            check(reopened_sheet.get_cell("D1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("E2").text_value() == "row2-gap-c2",
                "full-calc insert_columns styled formula post-noop output should read shifted source cells");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "full-calc insert_columns styled formula post-noop output should keep inserted coordinates absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "full-calc insert_columns styled formula post-noop edit save",
        8,
        [&sheet]() {
            sheet.set_cell("G2",
                fastxlsx::CellValue::text("post-noop-full-calc-insert-columns"));
        },
        inspect_full_calc_insert_columns_styled_post_noop_output);
    const auto post_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_output);
    check_contains(post_noop_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc insert_columns styled formula post-noop save should keep fullCalcOnLoad metadata");
    check(post_noop_entries.find("xl/calcChain.xml") == post_noop_entries.end(),
        "full-calc insert_columns styled formula post-noop save should not invent calcChain.xml");
}

void test_public_worksheet_editor_full_calculation_preserves_insert_columns_styled_formula_failed_save_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-columns-styled-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-columns-styled-failed-save-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-columns-styled-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-insert-columns-styled-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_columns(2, 2);
    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 7,
        "full-calc insert_columns styled formula failed save setup should keep shifted sparse count");

    editor.request_full_calculation();

    const auto source_entries_before_failed_save =
        fastxlsx::test::read_zip_entries(source);
    const auto check_dirty_insert_columns_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep diagnostics clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued workbook metadata edit before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the shifted worksheet dirty");
        check(sheet.cell_count() == dirty_cell_count,
            label + " should preserve the shifted sparse count");
        check(sheet.estimated_memory_usage() == dirty_memory_usage,
            label + " should preserve the shifted materialized memory estimate");
        check_cell_range_equals(sheet.used_range(), 1, 1, 3, 6,
            label + " should expose shifted bounds");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"Data"},
            label + " should report Data dirty");
        check(editor.pending_materialized_cell_count() == dirty_cell_count,
            label + " should report the shifted sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            label + " should report the shifted sparse memory");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, label + " dirty materialized summary");

        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("F2");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "A1+D1" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep translated formula and style id in memory");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                sheet.get_cell("D1").number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "placeholder-a2" &&
                sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                sheet.get_cell("E2").text_value() == "row2-gap-c2" &&
                sheet.get_cell("A3").text_value() == "extra-c3",
            label + " should keep shifted source cells in memory");
        check(!sheet.try_cell("B1").has_value() &&
                !sheet.try_cell("B2").has_value() &&
                !sheet.try_cell("C2").has_value(),
            label + " should keep inserted coordinates absent");
    };

    check_dirty_insert_columns_session(
        "full-calc insert_columns styled formula failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "full-calc insert_columns styled formula failed save should reject exact source overwrite");
    check_dirty_insert_columns_session(
        "full-calc insert_columns styled formula failed save rejected source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc insert_columns styled formula failed save should leave source package unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc insert_columns styled formula failed save safe retry should clean the shifted sheet");
    check(editor.has_pending_changes(),
        "full-calc insert_columns styled formula failed save safe retry should retain staged public changes");
    check(editor.pending_change_count() == 2,
        "full-calc insert_columns styled formula failed save safe retry should count metadata plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc insert_columns styled formula failed save safe retry should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc insert_columns styled formula failed save safe retry should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc insert_columns styled formula failed save safe retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_columns styled formula failed save safe retry should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc insert_columns styled formula failed save safe retry should leave source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="F2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+D1</f></c>)";
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "full-calc insert_columns styled formula failed save safe retry should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc insert_columns styled formula failed save safe retry should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:F3"/>)",
        "full-calc insert_columns styled formula failed save safe retry should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "full-calc insert_columns styled formula failed save safe retry should write shifted source-backed number");
    check_contains(worksheet_xml, R"(<c r="D2")",
        "full-calc insert_columns styled formula failed save safe retry should write shifted B2");
    check_contains(worksheet_xml, R"(<c r="E2")",
        "full-calc insert_columns styled formula failed save safe retry should write shifted C2");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc insert_columns styled formula failed save safe retry should write shifted styled formula");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "full-calc insert_columns styled formula failed save safe retry should omit inserted B1");
    check_not_contains(worksheet_xml, R"(r="B2")",
        "full-calc insert_columns styled formula failed save safe retry should omit inserted B2");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "full-calc insert_columns styled formula failed save safe retry should omit inserted C2");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc insert_columns styled formula failed save safe retry should preserve untouched worksheets");

    const auto inspect_full_calc_insert_columns_failed_save_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc insert_columns styled formula failed save no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "full-calc insert_columns styled formula failed save no-op reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_f2->text_value() == "A1+D1" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "full-calc insert_columns styled formula failed save no-op reopened output should read shifted formula style");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("D1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("A2").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("E2").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A3").text_value() == "extra-c3",
                "full-calc insert_columns styled formula failed save no-op reopened output should read shifted source cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 4 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 4 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_two[2].reference.row == 2 &&
                    reopened_row_two[2].reference.column == 5 &&
                    reopened_row_two[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_two[3].reference.row == 2 &&
                    reopened_row_two[3].reference.column == 6 &&
                    reopened_row_two[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_two[3].value.text_value() == "A1+D1" &&
                    reopened_row_two[3].value.has_style() &&
                    reopened_row_two[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc insert_columns styled formula failed save no-op reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_six =
                reopened_sheet.column_cells(6);
            check(reopened_column_six.size() == 1 &&
                    reopened_column_six[0].reference.row == 2 &&
                    reopened_column_six[0].reference.column == 6 &&
                    reopened_column_six[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_six[0].value.text_value() == "A1+D1" &&
                    reopened_column_six[0].value.has_style() &&
                    reopened_column_six[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc insert_columns styled formula failed save no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "full-calc insert_columns styled formula failed save no-op reopened output should keep inserted coordinates absent");
        };

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc insert_columns styled formula failed save no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc insert_columns styled formula failed save no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc insert_columns styled formula failed save no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc insert_columns styled formula failed save no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_columns styled formula failed save no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "full-calc insert_columns styled formula failed save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "full-calc insert_columns styled formula failed save no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc insert_columns styled formula failed save no-op output should match safe retry output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc insert_columns styled formula failed save no-op save should leave safe retry output unchanged");
    check_reopened_shift_output(
        noop_output,
        "full-calc insert_columns styled formula failed save no-op save",
        inspect_full_calc_insert_columns_failed_save_output);
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc insert_columns styled formula failed save no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc insert_columns styled formula failed save source after no-op");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc insert_columns styled formula failed save second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc insert_columns styled formula failed save second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc insert_columns styled formula failed save second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc insert_columns styled formula failed save second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc insert_columns styled formula failed save second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "full-calc insert_columns styled formula failed save second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "full-calc insert_columns styled formula failed save second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc insert_columns styled formula failed save second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc insert_columns styled formula failed save second no-op save should leave safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc insert_columns styled formula failed save second no-op save should leave first no-op output unchanged");
    check_reopened_shift_output(
        second_noop_output,
        "full-calc insert_columns styled formula failed save second no-op save",
        inspect_full_calc_insert_columns_failed_save_output);
    check_reopened_untouched_keep_me_output(
        second_noop_output,
        "full-calc insert_columns styled formula failed save second no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc insert_columns styled formula failed save source after second no-op");
}

void test_public_worksheet_editor_full_calculation_before_insert_columns_styled_formula_shift()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-styled-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-styled-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-styled-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-styled-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_columns styled formula setup should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "full-calc before insert_columns styled formula setup should queue one workbook metadata edit");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before insert_columns styled formula setup should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before insert_columns styled formula setup should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_columns styled formula setup should not expose dirty materialized memory");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check(!sheet.has_pending_changes(),
        "worksheet() after full-calc before insert_columns styled formula should materialize cleanly");
    check(editor.pending_change_count() == 1,
        "clean materialization after full-calc before insert_columns styled formula should keep metadata edit count");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "clean materialization after full-calc before insert_columns styled formula should keep dirty diagnostics clear");

    sheet.insert_columns(2, 2);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 7,
        "full-calc before insert_columns styled formula should keep shifted sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 6,
        "full-calc before insert_columns styled formula should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("F2");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+D1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "full-calc before insert_columns styled formula should translate formula and preserve style id");
    check(editor.pending_change_count() == 1,
        "full-calc before insert_columns styled formula should not flush materialized state before save_as");
    check(sheet.has_pending_changes(),
        "full-calc before insert_columns styled formula should leave the shifted sheet dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "full-calc before insert_columns styled formula should report Data dirty");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "full-calc before insert_columns styled formula should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "full-calc before insert_columns styled formula should report shifted sparse memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 1,
        "full-calc before insert_columns styled formula pre-save dirty summary");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc before insert_columns styled formula save_as should clean the shifted materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_columns styled formula save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before insert_columns styled formula save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before insert_columns styled formula save_as should clear dirty materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_columns styled formula save_as should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before insert_columns styled formula save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="F2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+D1</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc before insert_columns styled formula save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc before insert_columns styled formula save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:F3"/>)",
        "full-calc before insert_columns styled formula save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "full-calc before insert_columns styled formula save_as should write shifted source-backed number");
    check_contains(worksheet_xml, R"(<c r="D2")",
        "full-calc before insert_columns styled formula save_as should write shifted B2");
    check_contains(worksheet_xml, R"(<c r="E2")",
        "full-calc before insert_columns styled formula save_as should write shifted C2");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc before insert_columns styled formula save_as should write shifted styled formula");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "full-calc before insert_columns styled formula save_as should omit inserted B1");
    check_not_contains(worksheet_xml, R"(r="B2")",
        "full-calc before insert_columns styled formula save_as should omit inserted B2");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "full-calc before insert_columns styled formula save_as should omit inserted C2");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc before insert_columns styled formula should preserve untouched worksheets");

    const auto inspect_full_calc_before_insert_columns_styled_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc before insert_columns styled formula reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "full-calc before insert_columns styled formula reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_f2->text_value() == "A1+D1" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "full-calc before insert_columns styled formula reopened output should read shifted styled formula");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("A2").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("A3").text_value() == "extra-c3" &&
                    reopened_sheet.get_cell("D1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("E2").text_value() == "row2-gap-c2",
                "full-calc before insert_columns styled formula reopened output should read shifted source cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 4 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 4 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_two[2].reference.row == 2 &&
                    reopened_row_two[2].reference.column == 5 &&
                    reopened_row_two[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_two[3].reference.row == 2 &&
                    reopened_row_two[3].reference.column == 6 &&
                    reopened_row_two[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_two[3].value.text_value() == "A1+D1" &&
                    reopened_row_two[3].value.has_style() &&
                    reopened_row_two[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_columns styled formula reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_six =
                reopened_sheet.column_cells(6);
            check(reopened_column_six.size() == 1 &&
                    reopened_column_six[0].reference.row == 2 &&
                    reopened_column_six[0].reference.column == 6 &&
                    reopened_column_six[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_six[0].value.text_value() == "A1+D1" &&
                    reopened_column_six[0].value.has_style() &&
                    reopened_column_six[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_columns styled formula reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "full-calc before insert_columns styled formula reopened output should keep inserted coordinates absent");
        };
    check_reopened_shift_output(output,
        "full-calc before insert_columns styled formula",
        inspect_full_calc_before_insert_columns_styled_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_columns styled formula no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_columns styled formula no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before insert_columns styled formula no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_columns styled formula no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_columns styled formula no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "full-calc before insert_columns styled formula no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "full-calc before insert_columns styled formula no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc before insert_columns styled formula no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before insert_columns styled formula no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output,
        "full-calc before insert_columns styled formula no-op save",
        inspect_full_calc_before_insert_columns_styled_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_columns styled formula second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_columns styled formula second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before insert_columns styled formula second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_columns styled formula second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_columns styled formula second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "full-calc before insert_columns styled formula second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "full-calc before insert_columns styled formula second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc before insert_columns styled formula second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before insert_columns styled formula second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before insert_columns styled formula second no-op save should leave materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc before insert_columns styled formula second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "full-calc before insert_columns styled formula second no-op save",
        inspect_full_calc_before_insert_columns_styled_output);
}

void test_public_worksheet_editor_full_calculation_before_insert_columns_styled_formula_failed_save_preserves_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-styled-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-styled-failed-save-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-styled-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-styled-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.insert_columns(2, 2);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    const auto source_entries_before_failed_save =
        fastxlsx::test::read_zip_entries(source);
    check(dirty_cell_count == 7,
        "full-calc before insert_columns styled formula failed save setup should keep shifted sparse count");

    const auto check_dirty_insert_column_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep diagnostics clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued workbook metadata edit before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the shifted worksheet dirty");
        check(sheet.cell_count() == dirty_cell_count,
            label + " should preserve the shifted sparse count");
        check(sheet.estimated_memory_usage() == dirty_memory_usage,
            label + " should preserve the shifted materialized memory estimate");
        check_cell_range_equals(sheet.used_range(), 1, 1, 3, 6,
            label + " should expose shifted bounds");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"Data"},
            label + " should report Data dirty");
        check(editor.pending_materialized_cell_count() == dirty_cell_count,
            label + " should report the shifted sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            label + " should report the shifted sparse memory");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, label + " dirty materialized summary");

        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("F2");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "A1+D1" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep translated formula and style id in memory");
        check(sheet.get_cell("D1").number_value() == 1.0 &&
                sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                sheet.get_cell("E2").text_value() == "row2-gap-c2" &&
                sheet.get_cell("A3").text_value() == "extra-c3",
            label + " should keep shifted source cells in memory");
        check(!sheet.try_cell("B1").has_value() &&
                !sheet.try_cell("B2").has_value() &&
                !sheet.try_cell("C2").has_value(),
            label + " should keep inserted coordinates absent");
    };

    check_dirty_insert_column_session(
        "full-calc before insert_columns styled formula failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "full-calc before insert_columns styled formula failed save should reject exact source overwrite");
    check_dirty_insert_column_session(
        "full-calc before insert_columns styled formula failed save rejected source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc before insert_columns styled formula failed save should leave source package unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc before insert_columns styled formula failed save safe retry should clean the shifted sheet");
    check(editor.has_pending_changes(),
        "full-calc before insert_columns styled formula failed save safe retry should retain staged public changes");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_columns styled formula failed save safe retry should count metadata plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_columns styled formula failed save safe retry should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc before insert_columns styled formula failed save safe retry should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_columns styled formula failed save safe retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_columns styled formula failed save safe retry should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc before insert_columns styled formula failed save safe retry should leave source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="F2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+D1</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc before insert_columns styled formula failed save safe retry should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc before insert_columns styled formula failed save safe retry should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:F3"/>)",
        "full-calc before insert_columns styled formula failed save safe retry should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "full-calc before insert_columns styled formula failed save safe retry should write shifted source-backed number");
    check_contains(worksheet_xml, R"(<c r="D2")",
        "full-calc before insert_columns styled formula failed save safe retry should write shifted B2");
    check_contains(worksheet_xml, R"(<c r="E2")",
        "full-calc before insert_columns styled formula failed save safe retry should write shifted C2");
    check_contains(worksheet_xml, R"(<c r="A3" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "full-calc before insert_columns styled formula failed save safe retry should write source trailing row");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc before insert_columns styled formula failed save safe retry should write shifted styled formula");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "full-calc before insert_columns styled formula failed save safe retry should omit inserted B1");
    check_not_contains(worksheet_xml, R"(r="B2")",
        "full-calc before insert_columns styled formula failed save safe retry should omit inserted B2");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "full-calc before insert_columns styled formula failed save safe retry should omit inserted C2");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc before insert_columns styled formula failed save safe retry should preserve untouched worksheets");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_columns styled formula failed save no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_columns styled formula failed save no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before insert_columns styled formula failed save no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_columns styled formula failed save no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_columns styled formula failed save no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "full-calc before insert_columns styled formula failed save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "full-calc before insert_columns styled formula failed save no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc before insert_columns styled formula failed save no-op output should match safe retry output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before insert_columns styled formula failed save no-op save should leave safe retry output unchanged");
    check_reopened_shift_output(
        noop_output,
        "full-calc before insert_columns styled formula failed save no-op save",
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc before insert_columns styled formula failed save no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "full-calc before insert_columns styled formula failed save no-op reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_f2 = reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_f2->text_value() == "A1+D1" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "full-calc before insert_columns styled formula failed save no-op reopened output should read shifted formula style");
            check(reopened_sheet.get_cell("D1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("E2").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A3").text_value() == "extra-c3",
                "full-calc before insert_columns styled formula failed save no-op reopened output should read shifted source cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 4 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 4 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_two[2].reference.row == 2 &&
                    reopened_row_two[2].reference.column == 5 &&
                    reopened_row_two[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_two[3].reference.row == 2 &&
                    reopened_row_two[3].reference.column == 6 &&
                    reopened_row_two[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_two[3].value.text_value() == "A1+D1" &&
                    reopened_row_two[3].value.has_style() &&
                    reopened_row_two[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_columns styled formula failed save no-op reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_six =
                reopened_sheet.column_cells(6);
            check(reopened_column_six.size() == 1 &&
                    reopened_column_six[0].reference.row == 2 &&
                    reopened_column_six[0].reference.column == 6 &&
                    reopened_column_six[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_six[0].value.text_value() == "A1+D1" &&
                    reopened_column_six[0].value.has_style() &&
                    reopened_column_six[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_columns styled formula failed save no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "full-calc before insert_columns styled formula failed save no-op reopened output should keep inserted coordinates absent");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_columns styled formula failed save second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_columns styled formula failed save second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before insert_columns styled formula failed save second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before insert_columns styled formula failed save second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_columns styled formula failed save second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "full-calc before insert_columns styled formula failed save second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "full-calc before insert_columns styled formula failed save second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc before insert_columns styled formula failed save second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before insert_columns styled formula failed save second no-op save should leave safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc before insert_columns styled formula failed save second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(
        second_noop_output,
        "full-calc before insert_columns styled formula failed save second no-op save",
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc before insert_columns styled formula failed save second no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "full-calc before insert_columns styled formula failed save second no-op reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_f2 =
                reopened_sheet.try_cell("F2");
            check(reopened_f2.has_value() &&
                    reopened_f2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_f2->text_value() == "A1+D1" &&
                    reopened_f2->has_style() &&
                    reopened_f2->style_id().value() == styled_formula_style.value(),
                "full-calc before insert_columns styled formula failed save second no-op reopened output should read shifted formula style");
            check(reopened_sheet.get_cell("D1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("D2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("E2").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A3").text_value() == "extra-c3",
                "full-calc before insert_columns styled formula failed save second no-op reopened output should read shifted source cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_six =
                reopened_sheet.column_cells(6);
            check(reopened_column_six.size() == 1 &&
                    reopened_column_six[0].reference.row == 2 &&
                    reopened_column_six[0].reference.column == 6 &&
                    reopened_column_six[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_six[0].value.text_value() == "A1+D1" &&
                    reopened_column_six[0].value.has_style() &&
                    reopened_column_six[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before insert_columns styled formula failed save second no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value(),
                "full-calc before insert_columns styled formula failed save second no-op reopened output should keep inserted coordinates absent");
        });
    check_reopened_untouched_keep_me_output(
        noop_output,
        "full-calc before insert_columns styled formula failed save no-op Untouched");
    check_reopened_untouched_keep_me_output(
        second_noop_output,
        "full-calc before insert_columns styled formula failed save second no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc before insert_columns styled formula failed save source after no-op");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc before insert_columns styled formula failed save source after second no-op");
}

void test_public_worksheet_editor_full_calculation_before_insert_columns_shift()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-insert-columns-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_columns setup should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "full-calc before insert_columns setup should queue one workbook metadata edit");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before insert_columns setup should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before insert_columns setup should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_columns setup should not expose dirty materialized memory");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check(!sheet.has_pending_changes(),
        "worksheet() after full-calc before insert_columns should materialize cleanly");
    check(editor.pending_change_count() == 1,
        "clean materialization after full-calc before insert_columns should keep metadata edit count");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "clean materialization after full-calc before insert_columns should keep dirty diagnostics clear");

    sheet.set_cell(2, 3, fastxlsx::CellValue::formula("A1+B1"));
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("extra-c3"));
    sheet.insert_columns(2, 2);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 5,
        "full-calc before insert_columns should keep shifted sparse count");
    check(editor.pending_change_count() == 1,
        "full-calc before insert_columns should not flush materialized state before save_as");
    check(sheet.has_pending_changes(),
        "full-calc before insert_columns should leave the shifted sheet dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "full-calc before insert_columns should report Data dirty");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "full-calc before insert_columns should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "full-calc before insert_columns should report shifted sparse memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 1, "full-calc before insert_columns pre-save dirty summary");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc before insert_columns save_as should clean the shifted materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_columns save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before insert_columns save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before insert_columns save_as should clear dirty materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_columns save_as should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before insert_columns save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc before insert_columns save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc before insert_columns save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E3"/>)",
        "full-calc before insert_columns save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="D1"><v>1</v></c>)",
        "full-calc before insert_columns save_as should write shifted source-backed numeric cell");
    check_contains(worksheet_xml, R"(<c r="E2"><f>A1+D1</f></c>)",
        "full-calc before insert_columns save_as should write translated formula cell");
    check_contains(worksheet_xml, R"(<c r="E3")",
        "full-calc before insert_columns save_as should write shifted dirty cell");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "full-calc before insert_columns save_as should omit old shifted source coordinate");
    check_not_contains(worksheet_xml, R"(r="C2")",
        "full-calc before insert_columns save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="C3")",
        "full-calc before insert_columns save_as should omit old dirty coordinate");

    const auto inspect_full_calc_insert_columns_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "full-calc before insert_columns reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 5,
                "full-calc before insert_columns reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1->number_value() == 1.0,
                "full-calc before insert_columns reopened output should read shifted source number");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_e2.has_value() &&
                    reopened_e2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_e2->text_value() == "A1+D1",
                "full-calc before insert_columns reopened output should read translated formula");
            const std::optional<fastxlsx::CellValue> reopened_e3 =
                reopened_sheet.try_cell("E3");
            check(reopened_e3.has_value() &&
                    reopened_e3->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e3->text_value() == "extra-c3",
                "full-calc before insert_columns reopened output should read shifted dirty cell");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "full-calc before insert_columns reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "full-calc before insert_columns",
        inspect_full_calc_insert_columns_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_columns no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_columns no-op save should not record another workbook or materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_columns no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc before insert_columns no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc before insert_columns no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_columns no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc before insert_columns no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc before insert_columns no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc before insert_columns no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before insert_columns no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "full-calc before insert_columns no-op save",
        inspect_full_calc_insert_columns_output);
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc before insert_columns no-op Untouched");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before insert_columns second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before insert_columns second no-op save should not record another workbook or materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before insert_columns second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc before insert_columns second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc before insert_columns second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before insert_columns second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "full-calc before insert_columns second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "full-calc before insert_columns second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc before insert_columns second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before insert_columns second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before insert_columns second no-op save should leave materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc before insert_columns second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "full-calc before insert_columns second no-op save",
        inspect_full_calc_insert_columns_output);
    check_reopened_untouched_keep_me_output(
        second_noop_output, "full-calc before insert_columns second no-op Untouched");
}

void test_public_worksheet_editor_delete_columns_shifts_sparse_records()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-delete-columns-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-post-noop-output.xlsx");
    const std::filesystem::path post_noop_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-delete-columns-post-noop-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(1, 3, fastxlsx::CellValue::formula("B2+D1"));
    sheet.set_cell(2, 4, fastxlsx::CellValue::text("tail-d2"));
    sheet.delete_columns(1, 1);

    check(sheet.cell_count() == 3,
        "delete_columns should remove represented records in the deleted sparse columns");
    check(sheet.get_cell("A1").number_value() == 1.0,
        "delete_columns should shift later source-backed columns left");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("B1");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula
            && shifted_formula.text_value() == "A2+C1",
        "delete_columns should translate moved formula text by the column shift");
    check(sheet.get_cell("C2").text_value() == "tail-d2",
        "delete_columns should shift later dirty columns left");
    check(!sheet.try_cell("A2").has_value(),
        "delete_columns should remove represented cells from the deleted column");
    check(!sheet.try_cell("C1").has_value(),
        "delete_columns should remove the old shifted formula coordinate");
    check(!sheet.try_cell("D2").has_value(),
        "delete_columns should remove old shifted dirty coordinates");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "delete_columns should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_one = sheet.row_cells(1);
    check(shifted_row_one.size() == 2,
        "delete_columns row_cells should expose the shifted row snapshot");
    check(shifted_row_one[0].reference.row == 1 && shifted_row_one[0].reference.column == 1 &&
            shifted_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
            shifted_row_one[0].value.number_value() == 1.0,
        "delete_columns row_cells should keep the source-backed cell first");
    check(shifted_row_one[1].reference.row == 1 && shifted_row_one[1].reference.column == 2 &&
            shifted_row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_one[1].value.text_value() == "A2+C1",
        "delete_columns row_cells should keep the translated formula cell second");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_two =
        sheet.column_cells(2);
    check(shifted_column_two.size() == 1,
        "delete_columns column_cells should expose the shifted formula column snapshot");
    check(shifted_column_two[0].reference.row == 1 &&
            shifted_column_two[0].reference.column == 2 &&
            shifted_column_two[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_two[0].value.text_value() == "A2+C1",
        "delete_columns column_cells should keep the translated formula cell");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "delete_columns pre-save shift summary");
    check(sheet.has_pending_changes(),
        "delete_columns should dirty the materialized worksheet when records shift");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "delete_columns should report the dirty materialized worksheet name");
    check(editor.pending_materialized_cell_count() == 3,
        "delete_columns should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "delete_columns should report shifted sparse memory");
    check(!editor.last_edit_error().has_value(),
        "successful delete_columns should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "delete_columns save_as should project the shifted sparse dimension");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "delete_columns save_as should write the shifted source-backed numeric cell");
    check_contains(worksheet_xml, R"(<c r="B1"><f>A2+C1</f></c>)",
        "delete_columns save_as should write the translated formula cell");
    check_contains(worksheet_xml, R"(<c r="C2")",
        "delete_columns save_as should write the shifted dirty cell");
    check_not_contains(worksheet_xml, R"(r="C1")",
        "delete_columns save_as should not keep the old formula coordinate");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "delete_columns save_as should omit deleted column row-one text cells");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "delete_columns save_as should omit deleted column row-two text cells");
    const auto inspect_delete_columns_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "delete_columns reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "delete_columns reopened output should expose shifted used range");
            const std::optional<fastxlsx::CellValue> reopened_a1 =
                reopened_sheet.try_cell("A1");
            check(reopened_a1.has_value() &&
                    reopened_a1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1->number_value() == 1.0,
                "delete_columns reopened output should read shifted source B1 at A1");
            const std::optional<fastxlsx::CellValue> reopened_b1 =
                reopened_sheet.try_cell("B1");
            check(reopened_b1.has_value() &&
                    reopened_b1->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_b1->text_value() == "A2+C1",
                "delete_columns reopened output should read translated formula at B1");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c2->text_value() == "tail-d2",
                "delete_columns reopened output should read shifted dirty text at C2");
            check(!reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("D2").has_value(),
                "delete_columns reopened output should keep deleted and old coordinates absent");
        };
    check_reopened_shift_output(output, "delete_columns", inspect_delete_columns_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "delete_columns no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "delete_columns no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete_columns no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "delete_columns no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_columns no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "delete_columns no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "delete_columns no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "delete_columns no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "delete_columns no-op save",
        inspect_delete_columns_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_columns second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "delete_columns second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete_columns second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "delete_columns second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_columns second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop, "delete_columns second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop, "delete_columns second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "delete_columns second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_columns second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns second no-op save should leave the source package unchanged");
    check_reopened_shift_output(second_noop_output, "delete_columns second no-op save",
        inspect_delete_columns_output);

    sheet.set_cell("D2", fastxlsx::CellValue::text("post-noop-delete-columns-basic"));
    check(sheet.has_pending_changes(),
        "delete_columns post-noop edit should dirty the saved handle");
    check(sheet.cell_count() == 4,
        "delete_columns post-noop edit should add one sparse cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "delete_columns post-noop edit should expand bounds to D2");
    const std::optional<fastxlsx::CellValue> retained_formula = sheet.try_cell("B1");
    check(retained_formula.has_value() &&
            retained_formula->kind() == fastxlsx::CellValueKind::Formula &&
            retained_formula->text_value() == "A2+C1",
        "delete_columns post-noop edit should preserve the shifted formula text");
    check(editor.pending_change_count() == 1,
        "delete_columns post-noop edit should retain the prior handoff before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "delete_columns post-noop edit should report the dirty materialized sheet");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_columns post-noop save should clean the materialized handle");
    check(editor.pending_change_count() == 2,
        "delete_columns post-noop save should record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_columns post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_columns post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "delete_columns post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_columns post-noop save should leave the no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns post-noop save should leave the source package unchanged");
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<dimension ref="A1:D2"/>)",
        "delete_columns post-noop save should project the expanded sparse dimension");
    check_contains(post_noop_xml, R"(<c r="A1"><v>1</v></c>)",
        "delete_columns post-noop save should keep the shifted source-backed numeric cell");
    check_contains(post_noop_xml, R"(<c r="B1"><f>A2+C1</f></c>)",
        "delete_columns post-noop save should keep the translated formula cell");
    check_contains(post_noop_xml, R"(<c r="C2")",
        "delete_columns post-noop save should keep the shifted dirty cell");
    check_contains(post_noop_xml, R"(<c r="D2")",
        "delete_columns post-noop save should write the post-noop edit");
    check_contains(post_noop_xml, "post-noop-delete-columns-basic",
        "delete_columns post-noop save should write the post-noop edit text");
    check_not_contains(post_noop_xml, R"(r="C1")",
        "delete_columns post-noop save should not resurrect the old formula coordinate");
    check_not_contains(post_noop_xml, "placeholder-a1",
        "delete_columns post-noop save should not resurrect deleted row-one source text");
    check_not_contains(post_noop_xml, "placeholder-a2",
        "delete_columns post-noop save should not resurrect deleted row-two source text");

    const auto inspect_delete_columns_post_noop_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "delete_columns post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "delete_columns post-noop reopened output should expose expanded bounds");
            const std::optional<fastxlsx::CellValue> reopened_a1 =
                reopened_sheet.try_cell("A1");
            check(reopened_a1.has_value() &&
                    reopened_a1->kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1->number_value() == 1.0,
                "delete_columns post-noop reopened output should read shifted source B1 at A1");
            const std::optional<fastxlsx::CellValue> reopened_b1 =
                reopened_sheet.try_cell("B1");
            check(reopened_b1.has_value() &&
                    reopened_b1->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_b1->text_value() == "A2+C1",
                "delete_columns post-noop reopened output should read translated formula at B1");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c2->text_value() == "tail-d2",
                "delete_columns post-noop reopened output should read shifted dirty text at C2");
            const std::optional<fastxlsx::CellValue> reopened_d2 =
                reopened_sheet.try_cell("D2");
            check(reopened_d2.has_value() &&
                    reopened_d2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d2->text_value() == "post-noop-delete-columns-basic",
                "delete_columns post-noop reopened output should read the post-noop edit");
            check(!reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("B2").has_value() &&
                    !reopened_sheet.try_cell("C1").has_value(),
                "delete_columns post-noop reopened output should keep deleted and old coordinates absent");
        };
    check_reopened_shift_output(post_noop_output,
        "delete_columns post-noop save",
        inspect_delete_columns_post_noop_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(post_noop_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_columns post-noop noop save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "delete_columns post-noop noop save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_columns post-noop noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_columns post-noop noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns post-noop noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_post_noop_noop,
        "delete_columns post-noop noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_post_noop_noop,
        "delete_columns post-noop noop save");
    const auto post_noop_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_noop_output);
    check(post_noop_noop_entries == post_noop_entries,
        "delete_columns post-noop noop output should match post-noop output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns post-noop noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
        "delete_columns post-noop noop save should leave prior post-noop output unchanged");
    check_reopened_shift_output(post_noop_noop_output,
        "delete_columns post-noop noop save",
        inspect_delete_columns_post_noop_output);
}


} // namespace

int main()
{
    try {
            test_public_worksheet_editor_shift_snapshots_are_owning_across_later_shifts();
            test_public_worksheet_editor_delete_snapshots_are_owning_across_later_shifts();
            test_public_worksheet_editor_insert_rows_shifts_sparse_records();
            test_public_worksheet_editor_insert_rows_preserves_shifted_value_only_style();
            test_public_worksheet_editor_insert_rows_preserves_shifted_clear_value_style();
            test_public_worksheet_editor_full_calculation_preserves_insert_rows_shift();
            test_public_worksheet_editor_full_calculation_preserves_insert_rows_failed_save_state();
            test_public_worksheet_editor_full_calculation_before_insert_rows_styled_formula_shift();
            test_public_worksheet_editor_full_calculation_before_insert_rows_styled_formula_failed_save_preserves_state();
            test_public_worksheet_editor_insert_rows_shifted_sparse_snapshot();
            test_public_worksheet_editor_delete_rows_shifts_sparse_records();
            test_public_worksheet_editor_insert_columns_shifts_sparse_records();
            test_public_worksheet_editor_insert_columns_preserves_shifted_source_formula_style();
            test_public_worksheet_editor_insert_columns_preserves_shifted_value_only_style();
            test_public_worksheet_editor_insert_columns_preserves_shifted_clear_value_style();
            test_public_worksheet_editor_full_calculation_preserves_insert_columns_styled_formula_shift();
            test_public_worksheet_editor_full_calculation_preserves_insert_columns_styled_formula_failed_save_state();
            test_public_worksheet_editor_full_calculation_before_insert_columns_styled_formula_shift();
            test_public_worksheet_editor_full_calculation_before_insert_columns_styled_formula_failed_save_preserves_state();
            test_public_worksheet_editor_full_calculation_before_insert_columns_shift();
            test_public_worksheet_editor_delete_columns_shifts_sparse_records();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor public-state shift insertion tests passed\n");
    return 0;
}