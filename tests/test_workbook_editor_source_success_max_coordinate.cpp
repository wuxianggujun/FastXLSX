#include "test_workbook_editor_source_success_common.hpp"

bool source_max_coordinate_values_equal(
    const fastxlsx::CellValue& actual,
    const fastxlsx::CellValue& expected)
{
    if (actual.kind() != expected.kind()) {
        return false;
    }

    switch (expected.kind()) {
    case fastxlsx::CellValueKind::Blank:
        return true;
    case fastxlsx::CellValueKind::Number:
        return actual.number_value() == expected.number_value();
    case fastxlsx::CellValueKind::Boolean:
        return actual.boolean_value() == expected.boolean_value();
    case fastxlsx::CellValueKind::Text:
    case fastxlsx::CellValueKind::Formula:
    case fastxlsx::CellValueKind::Error:
        return actual.text_value() == expected.text_value();
    }

    return false;
}

bool source_max_coordinate_snapshot_matches(
    const fastxlsx::WorksheetCellSnapshot& actual,
    const fastxlsx::CellValue& expected)
{
    return actual.reference.row == 1048576 &&
        actual.reference.column == 16384 &&
        source_max_coordinate_values_equal(actual.value, expected);
}

void check_source_max_coordinate_live_edge_readback(
    fastxlsx::WorksheetEditor& sheet,
    const fastxlsx::CellValue& expected_edge,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.cell_count() == 4,
        prefix + " live readback should expose the restored sparse cell count");

    const std::optional<fastxlsx::CellRange> range = sheet.used_range();
    check(range.has_value() &&
            range->first_row == 1 &&
            range->first_column == 1 &&
            range->last_row == 1048576 &&
            range->last_column == 16384,
        prefix + " live readback should expose A1:XFD1048576 bounds");

    const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
    const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
    check(source_max_coordinate_values_equal(by_position, expected_edge),
        prefix + " live readback should read the edge through row/column overloads");
    check(source_max_coordinate_values_equal(by_a1, expected_edge),
        prefix + " live readback should read the edge through A1 overloads");

    const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
        sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
    check(edge_cells.size() == 1,
        prefix + " live edge range should expose one sparse record");
    if (edge_cells.size() == 1) {
        check(source_max_coordinate_snapshot_matches(edge_cells[0], expected_edge),
            prefix + " live edge range should preserve coordinates and value");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> max_row =
        sheet.row_cells(1048576);
    check(max_row.size() == 1,
        prefix + " live max row should expose only the edge record");
    if (max_row.size() == 1) {
        check(source_max_coordinate_snapshot_matches(max_row[0], expected_edge),
            prefix + " live max row should preserve the edge value");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> max_column =
        sheet.column_cells(16384);
    check(max_column.size() == 1,
        prefix + " live max column should expose only the edge record");
    if (max_column.size() == 1) {
        check(source_max_coordinate_snapshot_matches(max_column[0], expected_edge),
            prefix + " live max column should preserve the edge value");
    }

    check(sheet.has_pending_changes(),
        prefix + " live readback should keep the restored edit dirty before save");
}

void check_source_max_coordinate_read_only_noop_reopened_output(
    const std::filesystem::path& output,
    std::string_view scenario,
    const fastxlsx::CellValue& expected_edge)
{
    const std::string prefix(scenario);
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);

    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen");
    check(reopened_editor.pending_materialized_worksheet_names().empty(),
        prefix + " fresh reopen should not expose dirty materialized names");
    check(reopened_editor.pending_materialized_cell_count() == 0,
        prefix + " fresh reopen should not expose dirty materialized cells");
    check(reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " fresh reopen should not expose dirty materialized memory");
    check(!reopened_sheet.has_pending_changes(),
        prefix + " fresh reopen should materialize a clean worksheet");
    check(reopened_sheet.cell_count() == 4,
        prefix + " fresh reopen should keep the source sparse cell count");

    const std::optional<fastxlsx::CellRange> range = reopened_sheet.used_range();
    check(range.has_value() &&
            range->first_row == 1 &&
            range->first_column == 1 &&
            range->last_row == 1048576 &&
            range->last_column == 16384,
        prefix + " fresh reopen should expose A1:XFD1048576 bounds");

    const fastxlsx::CellValue by_position = reopened_sheet.get_cell(1048576, 16384);
    const fastxlsx::CellValue by_a1 = reopened_sheet.get_cell("XFD1048576");
    check(source_max_coordinate_values_equal(by_position, expected_edge),
        prefix + " fresh reopen should read the edge through row/column overloads");
    check(source_max_coordinate_values_equal(by_a1, expected_edge),
        prefix + " fresh reopen should read the edge through A1 overloads");

    const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
        reopened_sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
    check(edge_cells.size() == 1,
        prefix + " fresh reopen edge range should expose one sparse record");
    if (edge_cells.size() == 1) {
        check(source_max_coordinate_snapshot_matches(edge_cells[0], expected_edge),
            prefix + " fresh reopen edge range should preserve coordinates and value");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> max_row =
        reopened_sheet.row_cells(1048576);
    check(max_row.size() == 1,
        prefix + " fresh reopen max row should expose only the edge record");
    if (max_row.size() == 1) {
        check(source_max_coordinate_snapshot_matches(max_row[0], expected_edge),
            prefix + " fresh reopen max row should preserve the edge value");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> max_column =
        reopened_sheet.column_cells(16384);
    check(max_column.size() == 1,
        prefix + " fresh reopen max column should expose only the edge record");
    if (max_column.size() == 1) {
        check(source_max_coordinate_snapshot_matches(max_column[0], expected_edge),
            prefix + " fresh reopen max column should preserve the edge value");
    }

    check(!reopened_sheet.has_pending_changes(),
        prefix + " fresh reopen readback should leave the worksheet clean");
    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen readback");
}

void check_source_max_coordinate_erase_reopened_output(
    const std::filesystem::path& output,
    std::string_view scenario,
    std::string_view expected_a1_text,
    std::string_view expected_a2_text)
{
    const std::string prefix(scenario);
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);

    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen");
    check(reopened_editor.pending_materialized_worksheet_names().empty(),
        prefix + " fresh reopen should not expose dirty materialized names");
    check(reopened_editor.pending_materialized_cell_count() == 0,
        prefix + " fresh reopen should not expose dirty materialized cells");
    check(reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " fresh reopen should not expose dirty materialized memory");
    check(!reopened_sheet.has_pending_changes(),
        prefix + " fresh reopen should materialize a clean worksheet");
    check(reopened_sheet.cell_count() == 3,
        prefix + " fresh reopen should keep the shrunken sparse cell count");

    const std::optional<fastxlsx::CellRange> range = reopened_sheet.used_range();
    check(range.has_value() &&
            range->first_row == 1 &&
            range->first_column == 1 &&
            range->last_row == 2 &&
            range->last_column == 2,
        prefix + " fresh reopen should expose compact A1:B2 bounds");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        reopened_sheet.sparse_cells();
    check(all_cells.size() == 3 &&
            all_cells[0].reference.row == 1 &&
            all_cells[0].reference.column == 1 &&
            all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[0].value.text_value() == expected_a1_text &&
            all_cells[1].reference.row == 1 &&
            all_cells[1].reference.column == 2 &&
            all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
            all_cells[1].value.number_value() == 1.0 &&
            all_cells[2].reference.row == 2 &&
            all_cells[2].reference.column == 1 &&
            all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[2].value.text_value() == expected_a2_text,
        prefix + " fresh reopen sparse_cells should keep only A1, B1, and A2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        reopened_sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == expected_a1_text &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        prefix + " fresh reopen row_cells should keep compact row one");
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        reopened_sheet.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[0].value.text_value() == expected_a1_text &&
            column_one[1].reference.row == 2 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == expected_a2_text,
        prefix + " fresh reopen column_cells should keep compact column one");

    check(!reopened_sheet.try_cell("XFD1048576").has_value(),
        prefix + " fresh reopen should keep the erased edge absent");
    check(reopened_sheet.row_cells(1048576).empty(),
        prefix + " fresh reopen row_cells should keep the max row empty");
    check(reopened_sheet.column_cells(16384).empty(),
        prefix + " fresh reopen column_cells should keep the max column empty");
    check(reopened_sheet
              .sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384})
              .empty(),
        prefix + " fresh reopen range snapshot should keep the erased edge absent");
    check(!reopened_sheet.has_pending_changes(),
        prefix + " fresh reopen readback should leave the worksheet clean");
    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen readback");
}

void check_source_max_coordinate_erase_noop_save(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::filesystem::path& noop_output,
    const std::map<std::string, std::string>& erase_entries,
    const std::filesystem::path& source,
    const std::map<std::string, std::string>& source_entries,
    std::string_view scenario,
    std::string_view expected_a1_text,
    std::string_view expected_a2_text)
{
    const std::string prefix(scenario);
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        prefix + " no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        prefix + " no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " no-op save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        prefix + " no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, prefix + " no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, prefix + " no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == erase_entries,
        prefix + " no-op output should match the erase output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " no-op save should leave the source package unchanged");
    check_source_max_coordinate_erase_reopened_output(
        noop_output,
        prefix + " no-op output",
        expected_a1_text,
        expected_a2_text);
}

void check_source_max_coordinate_fresh_reopen_restore_after_erase(
    const std::filesystem::path& input,
    const std::filesystem::path& restored_output,
    const std::filesystem::path& restored_noop_output,
    const std::filesystem::path& source,
    const std::map<std::string, std::string>& source_entries,
    const std::map<std::string, std::string>& input_entries,
    const fastxlsx::CellValue& restored_edge,
    std::string_view restored_cell_xml,
    std::optional<std::string_view> shared_strings_append_xml,
    std::optional<std::string_view> shared_strings_count_xml,
    std::optional<std::string_view> shared_strings_unique_count_xml,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(input);
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);

    check(!reopened_sheet.has_pending_changes(),
        prefix + " input should start as a clean materialized worksheet");
    check(reopened_sheet.cell_count() == 3,
        prefix + " input should expose the erased sparse record count");
    check(!reopened_sheet.try_cell("XFD1048576").has_value(),
        prefix + " input should keep the erased edge absent");
    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " input");

    reopened_sheet.set_cell("XFD1048576", restored_edge);
    check(reopened_sheet.has_pending_changes(),
        prefix + " edit should dirty the fresh materialized handle");
    check(reopened_editor.has_pending_changes(),
        prefix + " edit should dirty the fresh editor");
    check(reopened_editor.pending_materialized_cell_count() == 4,
        prefix + " edit should restore the sparse source count");
    check_source_max_coordinate_live_edge_readback(
        reopened_sheet, restored_edge, prefix + " edit");

    reopened_editor.save_as(restored_output);
    check(!reopened_sheet.has_pending_changes(),
        prefix + " save should clean the materialized handle");
    check(reopened_editor.pending_change_count() == 1,
        prefix + " save should record one materialized handoff");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_editor, prefix + " save");
    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " save should keep diagnostics clear");

    const auto restored_entries = fastxlsx::test::read_zip_entries(restored_output);
    const std::string restored_xml = restored_entries.at("xl/worksheets/sheet1.xml");
    check_contains(restored_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        prefix + " output should restore max-bound dimension");
    check_contains(restored_xml, restored_cell_xml,
        prefix + " output should serialize the restored edge cell");
    if (shared_strings_append_xml.has_value() ||
        shared_strings_count_xml.has_value() ||
        shared_strings_unique_count_xml.has_value()) {
        const std::string restored_shared_strings =
            restored_entries.at("xl/sharedStrings.xml");
        if (shared_strings_append_xml.has_value()) {
            check_contains(restored_shared_strings,
                *shared_strings_append_xml,
                prefix + " output should append the restored shared string");
        }
        if (shared_strings_count_xml.has_value()) {
            check_contains(restored_shared_strings,
                *shared_strings_count_xml,
                prefix + " output should update sharedStrings count metadata");
        }
        if (shared_strings_unique_count_xml.has_value()) {
            check_contains(restored_shared_strings,
                *shared_strings_unique_count_xml,
                prefix + " output should update sharedStrings uniqueCount metadata");
        }
    }
    check(fastxlsx::test::read_zip_entries(input) == input_entries,
        prefix + " save should leave the fresh input package unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " save should leave the original source package unchanged");
    check_source_max_coordinate_read_only_noop_reopened_output(
        restored_output,
        prefix + " output",
        restored_edge);

    reopened_editor.save_as(restored_noop_output);
    check(!reopened_sheet.has_pending_changes(),
        prefix + " no-op save should keep the materialized handle clean");
    check(fastxlsx::test::read_zip_entries(restored_noop_output)
            == restored_entries,
        prefix + " no-op output should stay byte-identical");
    check(fastxlsx::test::read_zip_entries(input) == input_entries,
        prefix + " no-op save should leave the fresh input package unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " no-op save should leave the original source package unchanged");
    check_source_max_coordinate_read_only_noop_reopened_output(
        restored_noop_output,
        prefix + " no-op output",
        restored_edge);
}

void test_public_worksheet_editor_materializes_source_max_coordinate_and_erases_edge()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-source-max-coordinate-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-erase-output.xlsx");
    const std::filesystem::path erase_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-erase-noop-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-fresh-reopen-restore-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-fresh-reopen-restore-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-post-noop-reuse-noop-output.xlsx");

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    check(entries.find("xl/sharedStrings.xml") == entries.end(),
        "supported source values fixture should not require a sharedStrings part");
    check_not_contains(entries.at("xl/_rels/workbook.xml.rels"),
        "relationships/sharedStrings",
        "supported source values fixture should not require a sharedStrings relationship");
    check_not_contains(entries.at("[Content_Types].xml"),
        "spreadsheetml.sharedStrings+xml",
        "supported source values fixture should not require a sharedStrings content type");
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-max-a1</t></is></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="inlineStr"><is><t>source-max-a2</t></is></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="inlineStr"><is><t>source-max-edge</t></is></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                by_position.text_value() == "source-max-edge",
            "source max-coordinate materialization should read through row/column overloads");
        check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                by_a1.text_value() == "source-max-edge",
            "source max-coordinate materialization should read through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "source-max-edge",
                "source max-coordinate range snapshot should preserve source text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after source max-coordinate materialization should not mutate source package");
    check_source_max_coordinate_read_only_noop_reopened_output(
        noop_output,
        "source max-coordinate no-op output",
        fastxlsx::CellValue::text("source-max-edge"));

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "source-max-edge",
        "source max-coordinate erase output should omit the erased edge text");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-max-a1</t></is></c>)",
        "source max-coordinate erase output should preserve source A1");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-max-a2</t></is></c>)",
        "source max-coordinate erase output should preserve source A2");
    check_contains(erase_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "source max-coordinate erase output should preserve untouched sheets");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "source-max-edge",
        "source max-coordinate erase should not mutate the source package bytes");
    check_source_max_coordinate_erase_reopened_output(
        erase_output,
        "source max-coordinate erase output",
        "source-max-a1",
        "source-max-a2");
    check_source_max_coordinate_erase_noop_save(
        editor,
        sheet,
        erase_noop_output,
        erase_entries,
        source,
        source_entries,
        "source max-coordinate erase",
        "source-max-a1",
        "source-max-a2");

    const auto erase_noop_entries = fastxlsx::test::read_zip_entries(erase_noop_output);

    check_source_max_coordinate_fresh_reopen_restore_after_erase(
        erase_noop_output,
        fresh_reopen_restore_output,
        fresh_reopen_restore_noop_output,
        source,
        source_entries,
        erase_noop_entries,
        fastxlsx::CellValue::text("source-max-edge-fresh-reopen"),
        R"(<c r="XFD1048576" t="inlineStr"><is><t>source-max-edge-fresh-reopen</t></is></c>)",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "source max-coordinate fresh-reopen restore");

    sheet.set_cell("XFD1048576", fastxlsx::CellValue::text("source-max-edge-reused"));
    check(sheet.has_pending_changes(),
        "source max-coordinate post-noop reuse edit should dirty the materialized handle");
    check(editor.has_pending_changes(),
        "source max-coordinate post-noop reuse edit should dirty WorkbookEditor");
    check(editor.pending_materialized_cell_count() == 4,
        "source max-coordinate post-noop reuse edit should expose the restored sparse count");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate post-noop reuse save should clean the handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "source max-coordinate post-noop reuse save should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "source max-coordinate post-noop reuse save should clear dirty cell count");

    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "source max-coordinate post-noop reuse output should restore max-bound dimension");
    check_contains(post_noop_reuse_xml, "XFD1048576",
        "source max-coordinate post-noop reuse output should restore edge reference");
    check_contains(post_noop_reuse_xml, "source-max-edge-reused",
        "source max-coordinate post-noop reuse output should include the restored edge text");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate post-noop reuse save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(erase_output) == erase_entries,
        "source max-coordinate post-noop reuse save should not mutate the erase output");
    check(fastxlsx::test::read_zip_entries(erase_noop_output) == erase_noop_entries,
        "source max-coordinate post-noop reuse save should not mutate the erase no-op output");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_output,
        "source max-coordinate post-noop reuse output",
        fastxlsx::CellValue::text("source-max-edge-reused"));

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate post-noop reuse no-op save should keep the handle clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source max-coordinate post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate post-noop reuse no-op save should leave the source package unchanged");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_noop_output,
        "source max-coordinate post-noop reuse no-op output",
        fastxlsx::CellValue::text("source-max-edge-reused"));
}

void test_public_worksheet_editor_materializes_source_max_coordinate_formula_and_erases_edge()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-source-max-coordinate-formula-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-formula-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-formula-erase-output.xlsx");
    const std::filesystem::path erase_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-formula-erase-noop-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-formula-fresh-reopen-restore-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-formula-fresh-reopen-restore-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-formula-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-formula-post-noop-reuse-noop-output.xlsx");

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-formula-a1</t></is></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="inlineStr"><is><t>source-formula-a2</t></is></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576"><f>SUM(A1:B1)&amp;"&lt;source-edge&gt;"</f><v>12345</v></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<v>12345</v>",
        "source max-coordinate formula fixture should contain a stale cached value");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate formula materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate formula materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate formula materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate formula materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate formula materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate formula materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Formula &&
                by_position.text_value() == R"(SUM(A1:B1)&"<source-edge>")",
            "source max-coordinate formula materialization should ignore stale cached scalar values");
        check(by_a1.kind() == fastxlsx::CellValueKind::Formula &&
                by_a1.text_value() == R"(SUM(A1:B1)&"<source-edge>")",
            "source max-coordinate formula materialization should read formulas through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate formula range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate formula range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    edge_cells[0].value.text_value() == R"(SUM(A1:B1)&"<source-edge>")",
                "source max-coordinate formula range snapshot should preserve source formula text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate formula materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate formula materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate formula materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate formula materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after source max-coordinate formula materialization should not mutate source package");
    check_source_max_coordinate_read_only_noop_reopened_output(
        noop_output,
        "source max-coordinate formula no-op output",
        fastxlsx::CellValue::formula(R"(SUM(A1:B1)&"<source-edge>")"));

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate formula erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate formula erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate formula erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate formula erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate formula get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate formula range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate formula erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate formula erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate formula erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate formula erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate formula erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate formula erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate formula erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate formula erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate formula erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate formula erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate formula erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate formula erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate formula erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate formula erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "SUM(A1:B1)",
        "source max-coordinate formula erase output should omit the erased edge formula");
    check_not_contains(worksheet_xml, "12345",
        "source max-coordinate formula erase output should omit the stale cached scalar value");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-formula-a1</t></is></c>)",
        "source max-coordinate formula erase output should preserve source A1");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate formula erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-formula-a2</t></is></c>)",
        "source max-coordinate formula erase output should preserve source A2");
    check_contains(erase_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "source max-coordinate formula erase output should preserve untouched sheets");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<v>12345</v>",
        "source max-coordinate formula erase should not mutate the source package bytes");
    check_source_max_coordinate_erase_reopened_output(
        erase_output,
        "source max-coordinate formula erase output",
        "source-formula-a1",
        "source-formula-a2");
    check_source_max_coordinate_erase_noop_save(
        editor,
        sheet,
        erase_noop_output,
        erase_entries,
        source,
        source_entries,
        "source max-coordinate formula erase",
        "source-formula-a1",
        "source-formula-a2");

    const auto erase_noop_entries = fastxlsx::test::read_zip_entries(erase_noop_output);

    check_source_max_coordinate_fresh_reopen_restore_after_erase(
        erase_noop_output,
        fresh_reopen_restore_output,
        fresh_reopen_restore_noop_output,
        source,
        source_entries,
        erase_noop_entries,
        fastxlsx::CellValue::formula("SUM(A1:B1)+7"),
        R"(<c r="XFD1048576"><f>SUM(A1:B1)+7</f></c>)",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "source max-coordinate formula fresh-reopen restore");

    sheet.set_cell("XFD1048576", fastxlsx::CellValue::formula(R"(SUM(A1:B1)&"<reused-edge>")"));
    check(sheet.has_pending_changes(),
        "source max-coordinate formula post-noop reuse edit should dirty the materialized handle");
    check(editor.has_pending_changes(),
        "source max-coordinate formula post-noop reuse edit should dirty WorkbookEditor");
    check(editor.pending_materialized_cell_count() == 4,
        "source max-coordinate formula post-noop reuse edit should expose the restored sparse count");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate formula post-noop reuse save should clean the handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "source max-coordinate formula post-noop reuse save should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "source max-coordinate formula post-noop reuse save should clear dirty cell count");

    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "source max-coordinate formula post-noop reuse output should restore max-bound dimension");
    check_contains(
        post_noop_reuse_xml,
        R"(<c r="XFD1048576"><f>SUM(A1:B1)&amp;"&lt;reused-edge&gt;"</f></c>)",
        "source max-coordinate formula post-noop reuse output should restore formula text only");
    check_not_contains(post_noop_reuse_xml, "12345",
        "source max-coordinate formula post-noop reuse output should omit stale cached values");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate formula post-noop reuse save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(erase_output) == erase_entries,
        "source max-coordinate formula post-noop reuse save should not mutate the erase output");
    check(fastxlsx::test::read_zip_entries(erase_noop_output) == erase_noop_entries,
        "source max-coordinate formula post-noop reuse save should not mutate the erase no-op output");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_output,
        "source max-coordinate formula post-noop reuse output",
        fastxlsx::CellValue::formula(R"(SUM(A1:B1)&"<reused-edge>")"));

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate formula post-noop reuse no-op save should keep the handle clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source max-coordinate formula post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate formula post-noop reuse no-op save should leave the source package unchanged");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_noop_output,
        "source max-coordinate formula post-noop reuse no-op output",
        fastxlsx::CellValue::formula(R"(SUM(A1:B1)&"<reused-edge>")"));
}

void test_public_worksheet_editor_materializes_source_max_coordinate_error_and_erases_edge()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-source-max-coordinate-error-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-error-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-error-erase-output.xlsx");
    const std::filesystem::path erase_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-error-erase-noop-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-error-fresh-reopen-restore-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-error-fresh-reopen-restore-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-error-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-error-post-noop-reuse-noop-output.xlsx");

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-error-a1</t></is></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="inlineStr"><is><t>source-error-a2</t></is></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="e"><v>#VALUE!</v></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate error materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate error materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate error materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate error materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate error materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate error materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Error &&
                by_position.text_value() == "#VALUE!",
            "source max-coordinate error materialization should read errors through row/column overloads");
        check(by_a1.kind() == fastxlsx::CellValueKind::Error &&
                by_a1.text_value() == "#VALUE!",
            "source max-coordinate error materialization should read errors through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate error range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate error range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Error &&
                    edge_cells[0].value.text_value() == "#VALUE!",
                "source max-coordinate error range snapshot should preserve source error text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate error materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate error materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate error materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate error materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after source max-coordinate error materialization should not mutate source package");
    check_source_max_coordinate_read_only_noop_reopened_output(
        noop_output,
        "source max-coordinate error no-op output",
        fastxlsx::CellValue::error("#VALUE!"));

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate error erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate error erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate error erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate error erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate error get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate error range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate error erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate error erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate error erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate error erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate error erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate error erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate error erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate error erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate error erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate error erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate error erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate error erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate error erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate error erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "#VALUE!",
        "source max-coordinate error erase output should omit the erased edge error");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-error-a1</t></is></c>)",
        "source max-coordinate error erase output should preserve source A1");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate error erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-error-a2</t></is></c>)",
        "source max-coordinate error erase output should preserve source A2");
    check_contains(erase_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "source max-coordinate error erase output should preserve untouched sheets");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "#VALUE!",
        "source max-coordinate error erase should not mutate the source package bytes");
    check_source_max_coordinate_erase_reopened_output(
        erase_output,
        "source max-coordinate error erase output",
        "source-error-a1",
        "source-error-a2");
    check_source_max_coordinate_erase_noop_save(
        editor,
        sheet,
        erase_noop_output,
        erase_entries,
        source,
        source_entries,
        "source max-coordinate error erase",
        "source-error-a1",
        "source-error-a2");

    const auto erase_noop_entries = fastxlsx::test::read_zip_entries(erase_noop_output);

    check_source_max_coordinate_fresh_reopen_restore_after_erase(
        erase_noop_output,
        fresh_reopen_restore_output,
        fresh_reopen_restore_noop_output,
        source,
        source_entries,
        erase_noop_entries,
        fastxlsx::CellValue::error("#N/A"),
        R"(<c r="XFD1048576" t="e"><v>#N/A</v></c>)",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "source max-coordinate error fresh-reopen restore");

    sheet.set_cell("XFD1048576", fastxlsx::CellValue::error("#NULL!"));
    check(sheet.has_pending_changes(),
        "source max-coordinate error post-noop reuse edit should dirty the materialized handle");
    check(editor.has_pending_changes(),
        "source max-coordinate error post-noop reuse edit should dirty WorkbookEditor");
    check(editor.pending_materialized_cell_count() == 4,
        "source max-coordinate error post-noop reuse edit should expose the restored sparse count");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate error post-noop reuse save should clean the handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "source max-coordinate error post-noop reuse save should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "source max-coordinate error post-noop reuse save should clear dirty cell count");

    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "source max-coordinate error post-noop reuse output should restore max-bound dimension");
    check_contains(post_noop_reuse_xml,
        R"(<c r="XFD1048576" t="e"><v>#NULL!</v></c>)",
        "source max-coordinate error post-noop reuse output should restore error text");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate error post-noop reuse save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(erase_output) == erase_entries,
        "source max-coordinate error post-noop reuse save should not mutate the erase output");
    check(fastxlsx::test::read_zip_entries(erase_noop_output) == erase_noop_entries,
        "source max-coordinate error post-noop reuse save should not mutate the erase no-op output");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_output,
        "source max-coordinate error post-noop reuse output",
        fastxlsx::CellValue::error("#NULL!"));

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate error post-noop reuse no-op save should keep the handle clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source max-coordinate error post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate error post-noop reuse no-op save should leave the source package unchanged");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_noop_output,
        "source max-coordinate error post-noop reuse no-op output",
        fastxlsx::CellValue::error("#NULL!"));
}

void test_public_worksheet_editor_materializes_source_max_coordinate_shared_string_and_erases_edge()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-erase-output.xlsx");
    const std::filesystem::path erase_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-erase-noop-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-fresh-reopen-restore-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-fresh-reopen-restore-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-post-noop-reuse-noop-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions writer_options;
        writer_options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, writer_options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("source-shared-a1"),
            fastxlsx::CellView::number(1.0),
            fastxlsx::CellView::text("source-shared-edge & <max>")});
        data.append_row({fastxlsx::CellView::text("source-shared-a2")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-shared-edge")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/sharedStrings.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4" uniqueCount="4">)"
          R"(<si><t>source-shared-a1</t></si>)"
          R"(<si><t>source-shared-edge &amp; &lt;max&gt;</t></si>)"
          R"(<si><t>source-shared-a2</t></si>)"
          R"(<si><t>keep-shared-edge</t></si>)"
          R"(</sst>)";
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="s"><v>0</v></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="s"><v>2</v></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="s"><v>1</v></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before =
        source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, "source-shared-edge &amp; &lt;max&gt;",
        "source max-coordinate shared string fixture should contain the edge text");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate shared string fixture should store the edge cell as t=s");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate shared string materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate shared string materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate shared string materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate shared string materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate shared string materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate shared string materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                by_position.text_value() == "source-shared-edge & <max>",
            "source max-coordinate shared string materialization should decode XML entities");
        check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                by_a1.text_value() == "source-shared-edge & <max>",
            "source max-coordinate shared string materialization should read text through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate shared string range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate shared string range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "source-shared-edge & <max>",
                "source max-coordinate shared string range snapshot should preserve source text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate shared string materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate shared string materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate shared string materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate shared string materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after source max-coordinate shared string materialization should not mutate source package");
    check_source_max_coordinate_read_only_noop_reopened_output(
        noop_output,
        "source max-coordinate shared string no-op output",
        fastxlsx::CellValue::text("source-shared-edge & <max>"));

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate shared string erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate shared string erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate shared string erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate shared string erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate shared string get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate shared string range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate shared string erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate shared string erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate shared string erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate shared string erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate shared string erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate shared string erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate shared string erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate shared string erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate shared string erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate shared string erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate shared string erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate shared string erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate shared string erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate shared string erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "source-shared-edge",
        "source max-coordinate shared string erase output should omit the erased edge text");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "source max-coordinate shared string erase output should preserve source A1 as shared string index");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate shared string erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="s"><v>2</v></c>)",
        "source max-coordinate shared string erase output should preserve source A2 as shared string index");
    check(erase_entries.find("xl/sharedStrings.xml") != erase_entries.end()
            && erase_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "source max-coordinate shared string erase output should preserve source sharedStrings bytes");
    check(erase_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "source max-coordinate shared string erase output should preserve untouched sheets byte-for-byte");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate shared string erase should not mutate the source package bytes");
    check_source_max_coordinate_erase_reopened_output(
        erase_output,
        "source max-coordinate shared string erase output",
        "source-shared-a1",
        "source-shared-a2");
    check_source_max_coordinate_erase_noop_save(
        editor,
        sheet,
        erase_noop_output,
        erase_entries,
        source,
        source_entries,
        "source max-coordinate shared string erase",
        "source-shared-a1",
        "source-shared-a2");

    const auto erase_noop_entries = fastxlsx::test::read_zip_entries(erase_noop_output);

    check_source_max_coordinate_fresh_reopen_restore_after_erase(
        erase_noop_output,
        fresh_reopen_restore_output,
        fresh_reopen_restore_noop_output,
        source,
        source_entries,
        erase_noop_entries,
        fastxlsx::CellValue::text("source-shared-edge-fresh-reopen & <again>"),
        R"(<c r="XFD1048576" t="s"><v>4</v></c>)",
        R"(<si><t>source-shared-edge-fresh-reopen &amp; &lt;again&gt;</t></si></sst>)",
        R"(count="5")",
        R"(uniqueCount="5")",
        "source max-coordinate shared string fresh-reopen restore");

    sheet.set_cell(
        "XFD1048576",
        fastxlsx::CellValue::text("source-shared-edge-reused & <again>"));
    check(sheet.has_pending_changes(),
        "source max-coordinate shared string post-noop reuse edit should dirty the materialized handle");
    check(editor.has_pending_changes(),
        "source max-coordinate shared string post-noop reuse edit should dirty WorkbookEditor");
    check(editor.pending_materialized_cell_count() == 4,
        "source max-coordinate shared string post-noop reuse edit should expose the restored sparse count");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate shared string post-noop reuse save should clean the handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "source max-coordinate shared string post-noop reuse save should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "source max-coordinate shared string post-noop reuse save should clear dirty cell count");

    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "source max-coordinate shared string post-noop reuse output should restore max-bound dimension");
    check_contains(post_noop_reuse_xml, R"(<c r="XFD1048576" t="s"><v>4</v></c>)",
        "source max-coordinate shared string post-noop reuse output should append the edge index");
    const std::string post_noop_reuse_shared_strings =
        post_noop_reuse_entries.at("xl/sharedStrings.xml");
    check_contains(
        post_noop_reuse_shared_strings,
        R"(<si><t>source-shared-edge-reused &amp; &lt;again&gt;</t></si></sst>)",
        "source max-coordinate shared string post-noop reuse output should append the edge text");
    check_contains(post_noop_reuse_shared_strings, R"(count="5")",
        "source max-coordinate shared string post-noop reuse output should advance count metadata");
    check_contains(post_noop_reuse_shared_strings, R"(uniqueCount="5")",
        "source max-coordinate shared string post-noop reuse output should advance uniqueCount metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate shared string post-noop reuse save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(erase_output) == erase_entries,
        "source max-coordinate shared string post-noop reuse save should not mutate the erase output");
    check(fastxlsx::test::read_zip_entries(erase_noop_output) == erase_noop_entries,
        "source max-coordinate shared string post-noop reuse save should not mutate the erase no-op output");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_output,
        "source max-coordinate shared string post-noop reuse output",
        fastxlsx::CellValue::text("source-shared-edge-reused & <again>"));

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate shared string post-noop reuse no-op save should keep the handle clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source max-coordinate shared string post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate shared string post-noop reuse no-op save should leave the source package unchanged");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_noop_output,
        "source max-coordinate shared string post-noop reuse no-op output",
        fastxlsx::CellValue::text("source-shared-edge-reused & <again>"));
}

void test_public_worksheet_editor_materializes_source_max_coordinate_scalar_values_and_erases_edge()
{
    struct SourceMaxCoordinateScalarCase {
        std::string_view name;
        std::string_view edge_cell_xml;
        fastxlsx::CellValueKind expected_kind;
        double expected_number = 0.0;
        bool expected_boolean = false;
        std::string_view absent_payload;
    };

    const std::array<SourceMaxCoordinateScalarCase, 3> cases {{
        {"number",
            R"(<c r="XFD1048576"><v>9000.25</v></c>)",
            fastxlsx::CellValueKind::Number,
            9000.25,
            false,
            "9000.25"},
        {"boolean-false",
            R"(<c r="XFD1048576" t="b"><v>0</v></c>)",
            fastxlsx::CellValueKind::Boolean,
            0.0,
            false,
            R"(t="b")"},
        {"blank",
            R"(<c r="XFD1048576"/>)",
            fastxlsx::CellValueKind::Blank,
            0.0,
            false,
            R"(XFD1048576)"},
    }};

    for (const SourceMaxCoordinateScalarCase& case_info : cases) {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-source.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-noop-output.xlsx");
        const std::filesystem::path erase_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-erase-output.xlsx");
        const std::filesystem::path erase_noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-erase-noop-output.xlsx");
        const std::filesystem::path fresh_reopen_restore_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-fresh-reopen-restore-output.xlsx");
        const std::filesystem::path fresh_reopen_restore_noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-fresh-reopen-restore-noop-output.xlsx");
        const std::filesystem::path post_noop_reuse_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-post-noop-reuse-output.xlsx");
        const std::filesystem::path post_noop_reuse_noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-post-noop-reuse-noop-output.xlsx");

        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("placeholder")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-scalar-edge")});
            writer.close();
        }

        std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
        std::string worksheet_xml =
            std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
              R"(<dimension ref="A1:XFD1048576"/>)"
              R"(<sheetData>)"
              R"(<row r="1">)"
              R"(<c r="A1" t="inlineStr"><is><t>source-scalar-a1</t></is></c>)"
              R"(<c r="B1"><v>1</v></c>)"
              R"(</row>)"
              R"(<row r="2">)"
              R"(<c r="A2" t="inlineStr"><is><t>source-scalar-a2</t></is></c>)"
              R"(</row>)"
              R"(<row r="1048576">)";
        worksheet_xml.append(case_info.edge_cell_xml.data(), case_info.edge_cell_xml.size());
        worksheet_xml += R"(</row></sheetData></worksheet>)";
        entries.at("xl/worksheets/sheet1.xml") = worksheet_xml;
        write_stored_zip_entries(source, entries);
        const auto source_entries = fastxlsx::test::read_zip_entries(source);
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate scalar fixture should contain the edge cell");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

        check(sheet.cell_count() == 4,
            "source max-coordinate scalar materialization should load sparse source records only");
        check(!sheet.has_pending_changes(),
            "read-only source max-coordinate scalar materialization should start clean");
        check(!editor.has_pending_changes(),
            "read-only source max-coordinate scalar materialization should not dirty WorkbookEditor");
        check(editor.pending_change_count() == 0,
            "read-only source max-coordinate scalar materialization should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only source max-coordinate scalar materialization should not expose dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only source max-coordinate scalar materialization should not expose dirty cell count");

        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        if (case_info.expected_kind == fastxlsx::CellValueKind::Number) {
            check(by_position.kind() == fastxlsx::CellValueKind::Number &&
                    by_position.number_value() == case_info.expected_number,
                "source max-coordinate number should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Number &&
                    by_a1.number_value() == case_info.expected_number,
                "source max-coordinate number should materialize through A1 overloads");
        } else if (case_info.expected_kind == fastxlsx::CellValueKind::Boolean) {
            check(by_position.kind() == fastxlsx::CellValueKind::Boolean &&
                    by_position.boolean_value() == case_info.expected_boolean,
                "source max-coordinate boolean should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Boolean &&
                    by_a1.boolean_value() == case_info.expected_boolean,
                "source max-coordinate boolean should materialize through A1 overloads");
        } else {
            check(by_position.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate blank should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate blank should materialize through A1 overloads");
        }
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.size() == 1,
                "source max-coordinate scalar range snapshot should expose the edge record");
            if (edge_cells.size() == 1) {
                check(edge_cells[0].reference.row == 1048576 &&
                        edge_cells[0].reference.column == 16384,
                    "source max-coordinate scalar range snapshot should preserve legal boundary coordinates");
                check(edge_cells[0].value.kind() == case_info.expected_kind,
                    "source max-coordinate scalar range snapshot should preserve the source value kind");
            }
        }

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "no-op save_as after source max-coordinate scalar materialization should keep the handle clean");
        check(!editor.has_pending_changes(),
            "no-op save_as after source max-coordinate scalar materialization should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "no-op save_as after source max-coordinate scalar materialization should not create public edits");
        check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
            "no-op save_as after source max-coordinate scalar materialization should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "no-op save_as after source max-coordinate scalar materialization should not mutate source package");
        fastxlsx::CellValue expected_edge = fastxlsx::CellValue::blank();
        if (case_info.expected_kind == fastxlsx::CellValueKind::Number) {
            expected_edge = fastxlsx::CellValue::number(case_info.expected_number);
        } else if (case_info.expected_kind == fastxlsx::CellValueKind::Boolean) {
            expected_edge = fastxlsx::CellValue::boolean(case_info.expected_boolean);
        }
        check_source_max_coordinate_read_only_noop_reopened_output(
            noop_output,
            std::string("source max-coordinate scalar ") + std::string(case_info.name)
                + " no-op output",
            expected_edge);

        sheet.erase_cell("XFD1048576");
        check(!editor.last_edit_error().has_value(),
            "source max-coordinate scalar erase should not create edit diagnostics");
        check(sheet.has_pending_changes(),
            "source max-coordinate scalar erase should dirty the materialized handle");
        check(sheet.cell_count() == 3,
            "source max-coordinate scalar erase should shrink the sparse record count");
        check(!sheet.try_cell(1048576, 16384).has_value(),
            "source max-coordinate scalar erase should remove row/column readback");
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell("XFD1048576");
        }), "source max-coordinate scalar get_cell should throw after erase");
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.empty(),
                "source max-coordinate scalar range snapshot should be empty after erase");
        }
        {
            const std::vector<std::string> names =
                editor.pending_materialized_worksheet_names();
            check(names.size() == 1 && names[0] == "Data",
                "source max-coordinate scalar erase dirty diagnostics should use source sheet name");
        }
        check(editor.pending_materialized_cell_count() == 3,
            "source max-coordinate scalar erase dirty diagnostics should report remaining sparse records");

        editor.save_as(erase_output);
        check(!sheet.has_pending_changes(),
            "save_as after source max-coordinate scalar erase should clean the handle");
        check(editor.pending_change_count() == 1,
            "save_as after source max-coordinate scalar erase should count one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "save_as after source max-coordinate scalar erase should clear dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "save_as after source max-coordinate scalar erase should clear dirty cell count");
        check(editor.pending_worksheet_edits().empty(),
            "save_as after source max-coordinate scalar erase should clear summaries");

        const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
        const std::string erase_worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
        check_contains(erase_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "source max-coordinate scalar erase output should shrink dimension to remaining source records");
        check_not_contains(erase_worksheet_xml, "XFD1048576",
            "source max-coordinate scalar erase output should omit the erased edge reference");
        if (case_info.name != "blank") {
            check_not_contains(erase_worksheet_xml, case_info.absent_payload,
                "source max-coordinate scalar erase output should omit the erased edge payload");
        }
        check_contains(erase_worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>source-scalar-a1</t></is></c>)",
            "source max-coordinate scalar erase output should preserve source A1");
        check_contains(erase_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "source max-coordinate scalar erase output should preserve source B1");
        check_contains(erase_worksheet_xml,
            R"(<c r="A2" t="inlineStr"><is><t>source-scalar-a2</t></is></c>)",
            "source max-coordinate scalar erase output should preserve source A2");
        check(erase_entries.at("xl/worksheets/sheet2.xml") ==
                source_entries.at("xl/worksheets/sheet2.xml"),
            "source max-coordinate scalar erase output should preserve untouched sheets byte-for-byte");
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate scalar erase should not mutate the source package bytes");
        check_source_max_coordinate_erase_reopened_output(
            erase_output,
            "source max-coordinate scalar erase output",
            "source-scalar-a1",
            "source-scalar-a2");
        check_source_max_coordinate_erase_noop_save(
            editor,
            sheet,
            erase_noop_output,
            erase_entries,
            source,
            source_entries,
            "source max-coordinate scalar erase",
            "source-scalar-a1",
            "source-scalar-a2");

        const auto erase_noop_entries = fastxlsx::test::read_zip_entries(erase_noop_output);
        fastxlsx::CellValue post_noop_expected_edge = fastxlsx::CellValue::blank();
        std::string post_noop_expected_cell_xml = R"(<c r="XFD1048576"/>)";
        if (case_info.expected_kind == fastxlsx::CellValueKind::Number) {
            post_noop_expected_edge = fastxlsx::CellValue::number(7100.5);
            post_noop_expected_cell_xml = R"(<c r="XFD1048576"><v>7100.5</v></c>)";
        } else if (case_info.expected_kind == fastxlsx::CellValueKind::Boolean) {
            post_noop_expected_edge = fastxlsx::CellValue::boolean(true);
            post_noop_expected_cell_xml = R"(<c r="XFD1048576" t="b"><v>1</v></c>)";
        }

        check_source_max_coordinate_fresh_reopen_restore_after_erase(
            erase_noop_output,
            fresh_reopen_restore_output,
            fresh_reopen_restore_noop_output,
            source,
            source_entries,
            erase_noop_entries,
            post_noop_expected_edge,
            post_noop_expected_cell_xml,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::string("source max-coordinate scalar ") + std::string(case_info.name)
                + " fresh-reopen restore");

        sheet.set_cell("XFD1048576", post_noop_expected_edge);
        check(sheet.has_pending_changes(),
            "source max-coordinate scalar post-noop reuse edit should dirty the materialized handle");
        check(editor.has_pending_changes(),
            "source max-coordinate scalar post-noop reuse edit should dirty WorkbookEditor");
        check(editor.pending_materialized_cell_count() == 4,
            "source max-coordinate scalar post-noop reuse edit should expose the restored sparse count");

        editor.save_as(post_noop_reuse_output);
        check(!sheet.has_pending_changes(),
            "source max-coordinate scalar post-noop reuse save should clean the handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "source max-coordinate scalar post-noop reuse save should clear dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "source max-coordinate scalar post-noop reuse save should clear dirty cell count");

        const auto post_noop_reuse_entries =
            fastxlsx::test::read_zip_entries(post_noop_reuse_output);
        const std::string post_noop_reuse_xml =
            post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:XFD1048576"/>)",
            "source max-coordinate scalar post-noop reuse output should restore max-bound dimension");
        check_contains(post_noop_reuse_xml, post_noop_expected_cell_xml,
            "source max-coordinate scalar post-noop reuse output should restore the edge cell");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "source max-coordinate scalar post-noop reuse save should leave the source package unchanged");
        check(fastxlsx::test::read_zip_entries(erase_output) == erase_entries,
            "source max-coordinate scalar post-noop reuse save should not mutate the erase output");
        check(fastxlsx::test::read_zip_entries(erase_noop_output) == erase_noop_entries,
            "source max-coordinate scalar post-noop reuse save should not mutate the erase no-op output");
        check_source_max_coordinate_read_only_noop_reopened_output(
            post_noop_reuse_output,
            std::string("source max-coordinate scalar ") + std::string(case_info.name)
                + " post-noop reuse output",
            post_noop_expected_edge);

        editor.save_as(post_noop_reuse_noop_output);
        check(!sheet.has_pending_changes(),
            "source max-coordinate scalar post-noop reuse no-op save should keep the handle clean");
        check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
                == post_noop_reuse_entries,
            "source max-coordinate scalar post-noop reuse no-op save should keep output byte-stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "source max-coordinate scalar post-noop reuse no-op save should leave the source package unchanged");
        check_source_max_coordinate_read_only_noop_reopened_output(
            post_noop_reuse_noop_output,
            std::string("source max-coordinate scalar ") + std::string(case_info.name)
                + " post-noop reuse no-op output",
            post_noop_expected_edge);
    }
}

void test_public_worksheet_editor_materializes_source_max_coordinate_empty_inline_strings_and_erases_edge()
{
    struct SourceMaxCoordinateInlineCase {
        std::string_view name;
        std::string_view edge_cell_xml;
        fastxlsx::CellValueKind expected_kind;
        std::string_view absent_payload;
    };

    const std::array<SourceMaxCoordinateInlineCase, 2> cases {{
        {"empty-text",
            R"(<c r="XFD1048576" t="inlineStr"><is><t></t></is></c>)",
            fastxlsx::CellValueKind::Text,
            R"(<t></t>)"},
        {"inline-without-text",
            R"(<c r="XFD1048576" t="inlineStr"><is/></c>)",
            fastxlsx::CellValueKind::Blank,
            R"(<is/>)"},
    }};

    for (const SourceMaxCoordinateInlineCase& case_info : cases) {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-source.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-noop-output.xlsx");
        const std::filesystem::path erase_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-erase-output.xlsx");
        const std::filesystem::path erase_noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-erase-noop-output.xlsx");
        const std::filesystem::path fresh_reopen_restore_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-fresh-reopen-restore-output.xlsx");
        const std::filesystem::path fresh_reopen_restore_noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-fresh-reopen-restore-noop-output.xlsx");
        const std::filesystem::path post_noop_reuse_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-post-noop-reuse-output.xlsx");
        const std::filesystem::path post_noop_reuse_noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-post-noop-reuse-noop-output.xlsx");

        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("placeholder")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-empty-inline-edge")});
            writer.close();
        }

        std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
        std::string worksheet_xml =
            std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
              R"(<dimension ref="A1:XFD1048576"/>)"
              R"(<sheetData>)"
              R"(<row r="1">)"
              R"(<c r="A1" t="inlineStr"><is><t>source-empty-inline-a1</t></is></c>)"
              R"(<c r="B1"><v>1</v></c>)"
              R"(</row>)"
              R"(<row r="2">)"
              R"(<c r="A2" t="inlineStr"><is><t>source-empty-inline-a2</t></is></c>)"
              R"(</row>)"
              R"(<row r="1048576">)";
        worksheet_xml.append(case_info.edge_cell_xml.data(), case_info.edge_cell_xml.size());
        worksheet_xml += R"(</row></sheetData></worksheet>)";
        entries.at("xl/worksheets/sheet1.xml") = worksheet_xml;
        write_stored_zip_entries(source, entries);
        const auto source_entries = fastxlsx::test::read_zip_entries(source);
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate empty inline fixture should contain the edge cell");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

        check(sheet.cell_count() == 4,
            "source max-coordinate empty inline materialization should load sparse source records only");
        check(!sheet.has_pending_changes(),
            "read-only source max-coordinate empty inline materialization should start clean");
        check(!editor.has_pending_changes(),
            "read-only source max-coordinate empty inline materialization should not dirty WorkbookEditor");
        check(editor.pending_change_count() == 0,
            "read-only source max-coordinate empty inline materialization should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only source max-coordinate empty inline materialization should not expose dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only source max-coordinate empty inline materialization should not expose dirty cell count");

        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        if (case_info.expected_kind == fastxlsx::CellValueKind::Text) {
            check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                    by_position.text_value().empty(),
                "source max-coordinate empty inline text should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                    by_a1.text_value().empty(),
                "source max-coordinate empty inline text should materialize through A1 overloads");
        } else {
            check(by_position.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate inline string without text should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate inline string without text should materialize through A1 overloads");
        }
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.size() == 1,
                "source max-coordinate empty inline range snapshot should expose the edge record");
            if (edge_cells.size() == 1) {
                check(edge_cells[0].reference.row == 1048576 &&
                        edge_cells[0].reference.column == 16384,
                    "source max-coordinate empty inline range snapshot should preserve legal boundary coordinates");
                check(edge_cells[0].value.kind() == case_info.expected_kind,
                    "source max-coordinate empty inline range snapshot should preserve the source value kind");
                if (case_info.expected_kind == fastxlsx::CellValueKind::Text) {
                    check(edge_cells[0].value.text_value().empty(),
                        "source max-coordinate empty inline range snapshot should preserve empty text");
                }
            }
        }

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "no-op save_as after source max-coordinate empty inline materialization should keep the handle clean");
        check(!editor.has_pending_changes(),
            "no-op save_as after source max-coordinate empty inline materialization should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "no-op save_as after source max-coordinate empty inline materialization should not create public edits");
        check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
            "no-op save_as after source max-coordinate empty inline materialization should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "no-op save_as after source max-coordinate empty inline materialization should not mutate source package");
        const fastxlsx::CellValue expected_edge =
            case_info.expected_kind == fastxlsx::CellValueKind::Text
            ? fastxlsx::CellValue::text("")
            : fastxlsx::CellValue::blank();
        check_source_max_coordinate_read_only_noop_reopened_output(
            noop_output,
            std::string("source max-coordinate empty inline ") + std::string(case_info.name)
                + " no-op output",
            expected_edge);

        sheet.erase_cell("XFD1048576");
        check(!editor.last_edit_error().has_value(),
            "source max-coordinate empty inline erase should not create edit diagnostics");
        check(sheet.has_pending_changes(),
            "source max-coordinate empty inline erase should dirty the materialized handle");
        check(sheet.cell_count() == 3,
            "source max-coordinate empty inline erase should shrink the sparse record count");
        check(!sheet.try_cell(1048576, 16384).has_value(),
            "source max-coordinate empty inline erase should remove row/column readback");
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell("XFD1048576");
        }), "source max-coordinate empty inline get_cell should throw after erase");
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.empty(),
                "source max-coordinate empty inline range snapshot should be empty after erase");
        }
        {
            const std::vector<std::string> names =
                editor.pending_materialized_worksheet_names();
            check(names.size() == 1 && names[0] == "Data",
                "source max-coordinate empty inline erase dirty diagnostics should use source sheet name");
        }
        check(editor.pending_materialized_cell_count() == 3,
            "source max-coordinate empty inline erase dirty diagnostics should report remaining sparse records");

        editor.save_as(erase_output);
        check(!sheet.has_pending_changes(),
            "save_as after source max-coordinate empty inline erase should clean the handle");
        check(editor.pending_change_count() == 1,
            "save_as after source max-coordinate empty inline erase should count one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "save_as after source max-coordinate empty inline erase should clear dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "save_as after source max-coordinate empty inline erase should clear dirty cell count");
        check(editor.pending_worksheet_edits().empty(),
            "save_as after source max-coordinate empty inline erase should clear summaries");

        const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
        const std::string erase_worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
        check_contains(erase_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "source max-coordinate empty inline erase output should shrink dimension to remaining source records");
        check_not_contains(erase_worksheet_xml, "XFD1048576",
            "source max-coordinate empty inline erase output should omit the erased edge reference");
        check_not_contains(erase_worksheet_xml, case_info.absent_payload,
            "source max-coordinate empty inline erase output should omit the erased edge payload");
        check_contains(erase_worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>source-empty-inline-a1</t></is></c>)",
            "source max-coordinate empty inline erase output should preserve source A1");
        check_contains(erase_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "source max-coordinate empty inline erase output should preserve source B1");
        check_contains(erase_worksheet_xml,
            R"(<c r="A2" t="inlineStr"><is><t>source-empty-inline-a2</t></is></c>)",
            "source max-coordinate empty inline erase output should preserve source A2");
        check(erase_entries.at("xl/worksheets/sheet2.xml") ==
                source_entries.at("xl/worksheets/sheet2.xml"),
            "source max-coordinate empty inline erase output should preserve untouched sheets byte-for-byte");
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate empty inline erase should not mutate the source package bytes");
        check_source_max_coordinate_erase_reopened_output(
            erase_output,
            "source max-coordinate empty inline erase output",
            "source-empty-inline-a1",
            "source-empty-inline-a2");
        check_source_max_coordinate_erase_noop_save(
            editor,
            sheet,
            erase_noop_output,
            erase_entries,
            source,
            source_entries,
            "source max-coordinate empty inline erase",
            "source-empty-inline-a1",
            "source-empty-inline-a2");

        const auto erase_noop_entries = fastxlsx::test::read_zip_entries(erase_noop_output);
        fastxlsx::CellValue post_noop_expected_edge = fastxlsx::CellValue::blank();
        std::string post_noop_expected_cell_xml = R"(<c r="XFD1048576"/>)";
        if (case_info.expected_kind == fastxlsx::CellValueKind::Text) {
            post_noop_expected_edge = fastxlsx::CellValue::text("empty-inline-reused");
            post_noop_expected_cell_xml =
                R"(<c r="XFD1048576" t="inlineStr"><is><t>empty-inline-reused</t></is></c>)";
        }

        check_source_max_coordinate_fresh_reopen_restore_after_erase(
            erase_noop_output,
            fresh_reopen_restore_output,
            fresh_reopen_restore_noop_output,
            source,
            source_entries,
            erase_noop_entries,
            post_noop_expected_edge,
            post_noop_expected_cell_xml,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::string("source max-coordinate empty inline ") + std::string(case_info.name)
                + " fresh-reopen restore");

        sheet.set_cell("XFD1048576", post_noop_expected_edge);
        check(sheet.has_pending_changes(),
            "source max-coordinate empty inline post-noop reuse edit should dirty the materialized handle");
        check(editor.has_pending_changes(),
            "source max-coordinate empty inline post-noop reuse edit should dirty WorkbookEditor");
        check(editor.pending_materialized_cell_count() == 4,
            "source max-coordinate empty inline post-noop reuse edit should expose the restored sparse count");

        editor.save_as(post_noop_reuse_output);
        check(!sheet.has_pending_changes(),
            "source max-coordinate empty inline post-noop reuse save should clean the handle");
        check(editor.pending_materialized_worksheet_names().empty(),
            "source max-coordinate empty inline post-noop reuse save should clear dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "source max-coordinate empty inline post-noop reuse save should clear dirty cell count");

        const auto post_noop_reuse_entries =
            fastxlsx::test::read_zip_entries(post_noop_reuse_output);
        const std::string post_noop_reuse_xml =
            post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:XFD1048576"/>)",
            "source max-coordinate empty inline post-noop reuse output should restore max-bound dimension");
        check_contains(post_noop_reuse_xml, post_noop_expected_cell_xml,
            "source max-coordinate empty inline post-noop reuse output should restore the edge cell");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "source max-coordinate empty inline post-noop reuse save should leave the source package unchanged");
        check(fastxlsx::test::read_zip_entries(erase_output) == erase_entries,
            "source max-coordinate empty inline post-noop reuse save should not mutate the erase output");
        check(fastxlsx::test::read_zip_entries(erase_noop_output) == erase_noop_entries,
            "source max-coordinate empty inline post-noop reuse save should not mutate the erase no-op output");
        check_source_max_coordinate_read_only_noop_reopened_output(
            post_noop_reuse_output,
            std::string("source max-coordinate empty inline ") + std::string(case_info.name)
                + " post-noop reuse output",
            post_noop_expected_edge);

        editor.save_as(post_noop_reuse_noop_output);
        check(!sheet.has_pending_changes(),
            "source max-coordinate empty inline post-noop reuse no-op save should keep the handle clean");
        check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
                == post_noop_reuse_entries,
            "source max-coordinate empty inline post-noop reuse no-op save should keep output byte-stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "source max-coordinate empty inline post-noop reuse no-op save should leave the source package unchanged");
        check_source_max_coordinate_read_only_noop_reopened_output(
            post_noop_reuse_noop_output,
            std::string("source max-coordinate empty inline ") + std::string(case_info.name)
                + " post-noop reuse no-op output",
            post_noop_expected_edge);
    }
}

void test_public_worksheet_editor_materializes_source_max_coordinate_rich_shared_string_and_erases_edge()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-erase-output.xlsx");
    const std::filesystem::path erase_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-erase-noop-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-fresh-reopen-restore-output.xlsx");
    const std::filesystem::path fresh_reopen_restore_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-fresh-reopen-restore-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-post-noop-reuse-noop-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::number(7.0)});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    const std::string rich_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="3" uniqueCount="3">)"
        R"(<si><t>source-rich-a1</t></si>)"
        R"(<si><r><t>rich-</t></r><r><t>A&amp;B </t></r><r><t>&lt;edge&gt;</t></r><rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh><phoneticPr fontId="1"/><extLst><ext uri="{fastxlsx-test}"><t>ignored-ext</t></ext></extLst></si>)"
        R"(<si><t>source-rich-a2</t></si>)"
        R"(</sst>)";
    entries.at("xl/sharedStrings.xml") = rich_shared_strings;
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="s"><v>0</v></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="s"><v>2</v></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="s"><v>1</v></c>)"
          R"(</row></sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, R"(<rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh>)",
        "source rich shared string fixture should contain ignored phonetic text");
    check_contains(shared_strings_before, R"(<ext uri="{fastxlsx-test}"><t>ignored-ext</t></ext>)",
        "source rich shared string fixture should contain ignored extension text");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate rich shared string fixture should store the edge as t=s");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate rich shared string materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate rich shared string materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate rich shared string materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate rich shared string materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate rich shared string materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate rich shared string materialization should not expose dirty cell count");

    const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
    const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
    check(by_position.kind() == fastxlsx::CellValueKind::Text &&
            by_position.text_value() == "rich-A&B <edge>",
        "source max-coordinate rich shared string should flatten runs through row/column overloads");
    check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
            by_a1.text_value() == "rich-A&B <edge>",
        "source max-coordinate rich shared string should flatten runs through A1 overloads");
    check(sheet.get_cell("A1").text_value() == "source-rich-a1",
        "source rich shared string fixture should materialize A1 beside the edge");
    check(sheet.get_cell("A2").text_value() == "source-rich-a2",
        "source rich shared string fixture should materialize A2 beside the edge");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate rich shared string range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate rich shared string range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "rich-A&B <edge>",
                "source max-coordinate rich shared string range snapshot should preserve flattened text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate rich shared string materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate rich shared string materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate rich shared string materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate rich shared string materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after source max-coordinate rich shared string materialization should not mutate source package");
    check_source_max_coordinate_read_only_noop_reopened_output(
        noop_output,
        "source max-coordinate rich shared string no-op output",
        fastxlsx::CellValue::text("rich-A&B <edge>"));

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate rich shared string erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate rich shared string erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate rich shared string erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate rich shared string erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate rich shared string get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate rich shared string range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate rich shared string erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate rich shared string erase dirty diagnostics should report remaining sparse records");

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate rich shared string erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate rich shared string erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate rich shared string erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate rich shared string erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate rich shared string erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string erase_worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(erase_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate rich shared string erase output should shrink dimension to remaining source records");
    check_not_contains(erase_worksheet_xml, "XFD1048576",
        "source max-coordinate rich shared string erase output should omit the erased edge reference");
    check_not_contains(erase_worksheet_xml, "rich-A&amp;B",
        "source max-coordinate rich shared string erase output should omit the erased flattened text");
    check_contains(erase_worksheet_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "source max-coordinate rich shared string erase output should project source A1 as shared string index");
    check_contains(erase_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate rich shared string erase output should preserve source B1");
    check_contains(erase_worksheet_xml,
        R"(<c r="A2" t="s"><v>2</v></c>)",
        "source max-coordinate rich shared string erase output should project source A2 as shared string index");
    check(erase_entries.find("xl/sharedStrings.xml") != erase_entries.end() &&
            erase_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "source max-coordinate rich shared string erase output should preserve source sharedStrings bytes");
    check(erase_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "source max-coordinate rich shared string erase output should preserve untouched sheets byte-for-byte");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate rich shared string erase should not mutate the source package bytes");
    check_source_max_coordinate_erase_reopened_output(
        erase_output,
        "source max-coordinate rich shared string erase output",
        "source-rich-a1",
        "source-rich-a2");
    check_source_max_coordinate_erase_noop_save(
        editor,
        sheet,
        erase_noop_output,
        erase_entries,
        source,
        source_entries,
        "source max-coordinate rich shared string erase",
        "source-rich-a1",
        "source-rich-a2");

    const auto erase_noop_entries = fastxlsx::test::read_zip_entries(erase_noop_output);

    check_source_max_coordinate_fresh_reopen_restore_after_erase(
        erase_noop_output,
        fresh_reopen_restore_output,
        fresh_reopen_restore_noop_output,
        source,
        source_entries,
        erase_noop_entries,
        fastxlsx::CellValue::text("rich-shared-edge-fresh-reopen & <again>"),
        R"(<c r="XFD1048576" t="s"><v>3</v></c>)",
        R"(<si><t>rich-shared-edge-fresh-reopen &amp; &lt;again&gt;</t></si></sst>)",
        R"(count="4")",
        R"(uniqueCount="4")",
        "source max-coordinate rich shared string fresh-reopen restore");

    sheet.set_cell(
        "XFD1048576",
        fastxlsx::CellValue::text("rich-shared-edge-reused & <again>"));
    check(sheet.has_pending_changes(),
        "source max-coordinate rich shared string post-noop reuse edit should dirty the materialized handle");
    check(editor.has_pending_changes(),
        "source max-coordinate rich shared string post-noop reuse edit should dirty WorkbookEditor");
    check(editor.pending_materialized_cell_count() == 4,
        "source max-coordinate rich shared string post-noop reuse edit should expose the restored sparse count");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate rich shared string post-noop reuse save should clean the handle");
    check(editor.pending_materialized_worksheet_names().empty(),
        "source max-coordinate rich shared string post-noop reuse save should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "source max-coordinate rich shared string post-noop reuse save should clear dirty cell count");

    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:XFD1048576"/>)",
        "source max-coordinate rich shared string post-noop reuse output should restore max-bound dimension");
    check_contains(post_noop_reuse_xml, R"(<c r="XFD1048576" t="s"><v>3</v></c>)",
        "source max-coordinate rich shared string post-noop reuse output should append the edge index");
    const std::string post_noop_reuse_shared_strings =
        post_noop_reuse_entries.at("xl/sharedStrings.xml");
    check_contains(
        post_noop_reuse_shared_strings,
        R"(<si><t>rich-shared-edge-reused &amp; &lt;again&gt;</t></si></sst>)",
        "source max-coordinate rich shared string post-noop reuse output should append the edge text");
    check_contains(post_noop_reuse_shared_strings, R"(count="4")",
        "source max-coordinate rich shared string post-noop reuse output should advance count metadata");
    check_contains(post_noop_reuse_shared_strings, R"(uniqueCount="4")",
        "source max-coordinate rich shared string post-noop reuse output should advance uniqueCount metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate rich shared string post-noop reuse save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(erase_output) == erase_entries,
        "source max-coordinate rich shared string post-noop reuse save should not mutate the erase output");
    check(fastxlsx::test::read_zip_entries(erase_noop_output) == erase_noop_entries,
        "source max-coordinate rich shared string post-noop reuse save should not mutate the erase no-op output");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_output,
        "source max-coordinate rich shared string post-noop reuse output",
        fastxlsx::CellValue::text("rich-shared-edge-reused & <again>"));

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source max-coordinate rich shared string post-noop reuse no-op save should keep the handle clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source max-coordinate rich shared string post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source max-coordinate rich shared string post-noop reuse no-op save should leave the source package unchanged");
    check_source_max_coordinate_read_only_noop_reopened_output(
        post_noop_reuse_noop_output,
        "source max-coordinate rich shared string post-noop reuse no-op output",
        fastxlsx::CellValue::text("rich-shared-edge-reused & <again>"));
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_materializes_source_max_coordinate_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_formula_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_error_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_shared_string_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_scalar_values_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_empty_inline_strings_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_rich_shared_string_and_erases_edge();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor source-success max-coordinate check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor source-success max-coordinate tests passed\n");
    return 0;
}
