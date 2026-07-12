#include "test_workbook_editor_public_state_support.hpp"

namespace {

void test_public_worksheet_editor_shift_handle_reuse_after_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reuse-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reuse-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reuse-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reuse-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reuse-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reuse-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check(sheet.has_pending_changes(),
        "shift handle reuse should dirty the borrowed handle before the first save");
    check(sheet.cell_count() == 3,
        "shift handle reuse should keep sparse count after the first shift");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 2,
        "shift handle reuse should expose first-shift bounds before save");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift handle reuse first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift handle reuse first save should record the flushed materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift handle reuse first save should clear aggregate dirty materialized diagnostics");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift handle reuse first save should keep shifted cells readable on the same handle");

    sheet.insert_columns(2, 1);
    check(sheet.has_pending_changes(),
        "shift handle reuse should dirty the same borrowed handle after the second shift");
    check(sheet.cell_count() == 3,
        "shift handle reuse should keep sparse count after the second shift");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "shift handle reuse should keep cells left of the inserted column");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "shift handle reuse should shift source-backed B1 to C1 after save");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift handle reuse should keep the prior row shift after the column shift");
    check(!sheet.try_cell("B1").has_value() && !sheet.try_cell("A2").has_value(),
        "shift handle reuse should keep old sparse coordinates absent after both shifts");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 3,
        "shift handle reuse should refresh bounds after the second shift");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes(),
        "shift handle reuse second save should clean the reused borrowed handle");
    check(editor.pending_change_count() == 2,
        "shift handle reuse second save should record a second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift handle reuse second save should clear aggregate dirty materialized diagnostics");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "shift handle reuse no-op save should keep the reused handle clean");
    check(editor.pending_change_count() == 2,
        "shift handle reuse no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift handle reuse no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift handle reuse no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift handle reuse no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift handle reuse no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift handle reuse no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "shift handle reuse no-op output should match the second output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "shift handle reuse second no-op save should keep the reused handle clean");
    check(editor.pending_change_count() == 2,
        "shift handle reuse second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift handle reuse second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift handle reuse second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift handle reuse second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift handle reuse second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift handle reuse second no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift handle reuse second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift handle reuse second no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift handle reuse second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift handle reuse second no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift handle reuse second no-op output should match the first no-op output");

    sheet.set_cell("D3", fastxlsx::CellValue::text("handle-reuse-post-noop"));
    check(sheet.has_pending_changes(),
        "shift handle reuse post-noop edit should dirty the reused handle");
    check(sheet.cell_count() == 4,
        "shift handle reuse post-noop edit should add one sparse cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 4,
        "shift handle reuse post-noop edit should expand bounds to D3");
    check(sheet.get_cell("D3").text_value() == "handle-reuse-post-noop",
        "shift handle reuse post-noop edit should expose the new dirty cell");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("C1").number_value() == 1.0,
        "shift handle reuse post-noop edit should preserve shifted source cells");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 2, "shift handle reuse post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "shift handle reuse post-noop save should clean the reused handle");
    check(editor.pending_change_count() == 3,
        "shift handle reuse post-noop save should record the third materialized handoff");
    check(editor.has_pending_changes(),
        "shift handle reuse post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift handle reuse post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift handle reuse post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift handle reuse post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift handle reuse post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift handle reuse post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift handle reuse post-noop save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift handle reuse post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift handle reuse post-noop save should leave the second no-op output unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<dimension ref="A1:D3"/>)",
        "shift handle reuse post-noop output should expand bounds to D3");
    check_contains(post_noop_xml, R"(<c r="D3")",
        "shift handle reuse post-noop output should write the later D3 cell");
    check_contains(post_noop_xml, "handle-reuse-post-noop",
        "shift handle reuse post-noop output should write the later D3 text");
    check_not_contains(post_noop_xml, R"(r="B1")",
        "shift handle reuse post-noop output should keep old B1 absent");
    check_not_contains(post_noop_xml, R"(r="A2")",
        "shift handle reuse post-noop output should keep old A2 absent");

    check_reopened_shift_output(first_output, "shift handle reuse first save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift handle reuse first save should reopen with first-shift sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift handle reuse first save should reopen with first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift handle reuse first save should keep B1 before the later column shift");
            check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "shift handle reuse first save should keep shifted A2 at A3");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift handle reuse first save should not include later or old coordinates");
        });
    check_reopened_shift_output(second_output, "shift handle reuse second save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift handle reuse second save should reopen with second-shift sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift handle reuse second save should reopen with second-shift bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift handle reuse second save should keep shifted B1 at C1");
            check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "shift handle reuse second save should keep the prior row shift");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift handle reuse second save should keep old sparse coordinates absent");
        });
    check_reopened_shift_output(second_noop_output, "shift handle reuse second no-op output",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift handle reuse second no-op output should reopen with second-shift sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift handle reuse second no-op output should reopen with second-shift bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift handle reuse second no-op output should keep shifted B1 at C1");
            check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "shift handle reuse second no-op output should keep the prior row shift");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift handle reuse second no-op output should keep old sparse coordinates absent");
        });
    check_reopened_shift_output(post_noop_output, "shift handle reuse post-noop output",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift handle reuse post-noop output should reopen with post-noop sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 4,
                "shift handle reuse post-noop output should reopen with post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift handle reuse post-noop output should keep shifted B1 at C1");
            check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "shift handle reuse post-noop output should keep shifted A2");
            check(reopened_sheet.get_cell("D3").text_value() == "handle-reuse-post-noop",
                "shift handle reuse post-noop output should keep the later edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 4 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "handle-reuse-post-noop",
                "shift handle reuse post-noop row_cells should expose shifted row order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                reopened_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 3 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "handle-reuse-post-noop",
                "shift handle reuse post-noop column_cells should expose the later edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift handle reuse post-noop output should keep old sparse coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_reuses_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-second-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-repeat-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check(sheet.has_pending_changes(),
        "shift reacquire should dirty the borrowed handle before the first save");
    check(sheet.cell_count() == 3,
        "shift reacquire should keep sparse count after the first shift");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire should expose the first shifted source row before save");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire first save should clean the original borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire first save should clear dirty materialized diagnostics");
    check(editor.pending_change_count() == 1,
        "shift reacquire first save should record the materialized handoff");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire should return the saved clean materialized session");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "shift reacquire should preserve sparse count on both handles");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire should reuse the saved shifted state instead of reloading source");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire should keep old shifted coordinates absent on both handles");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "shift reacquire later shift should dirty the shared session on both handles");
    const std::vector<std::string> dirty_names = editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 1 && dirty_names[0] == "Data",
        "shift reacquire later shift should report only Data as dirty materialized");
    check(editor.pending_materialized_cell_count() == 3,
        "shift reacquire later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "shift reacquire later shift should report the shared memory estimate");
    const fastxlsx::CellValue shifted_number = reacquired.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "shift reacquire later shift should move the source-backed number");
    const fastxlsx::CellValue old_handle_number = sheet.get_cell("C1");
    check(old_handle_number.kind() == fastxlsx::CellValueKind::Number &&
            old_handle_number.number_value() == 1.0,
        "shift reacquire later shift should be visible through the older handle");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire later shift should keep the prior row shift on both handles");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() && !reacquired.try_cell("A2").has_value(),
        "shift reacquire later shift should keep old sparse coordinates absent on both handles");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire second save should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire second save should clear dirty materialized diagnostics again");
    check(editor.pending_change_count() == 2,
        "shift reacquire second save should record the second materialized handoff");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire first output should keep the first shifted bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire first output should keep B1 before the later column shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire first output should contain the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire first output should not include the later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire first output should omit the old row coordinate");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire second output should project the combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire second output should contain the later column shift");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire second output should retain the earlier row shift");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire second output should omit the old column coordinate");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire second output should keep the old row coordinate absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift reacquire second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire second no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "shift reacquire second no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire second no-op save should leave the second output unchanged");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire repeat no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire repeat no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire repeat no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift reacquire repeat no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire repeat no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire repeat no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire repeat no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire repeat no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire repeat no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire repeat no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire repeat no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire repeat no-op output should match the first no-op output");

    reacquired.set_cell("D3", fastxlsx::CellValue::text("reacquire-post-noop-reuse"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "shift reacquire reuse post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "shift reacquire reuse post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 4,
        "shift reacquire reuse post-noop edit should expand bounds to D3");
    check(reacquired.get_cell("D3").text_value() == "reacquire-post-noop-reuse" &&
            sheet.get_cell("D3").text_value() == "reacquire-post-noop-reuse",
        "shift reacquire reuse post-noop edit should expose the new dirty cell through both handles");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire reuse post-noop edit should preserve prior shifted source cells");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 2, "shift reacquire reuse post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire reuse post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 3,
        "shift reacquire reuse post-noop save should record the third materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire reuse post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire reuse post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift reacquire reuse post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire reuse post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire reuse post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire reuse post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire reuse post-noop save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire reuse post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire reuse post-noop save should leave the repeat no-op output unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<dimension ref="A1:D3"/>)",
        "shift reacquire reuse post-noop output should expand bounds to D3");
    check_contains(post_noop_xml, R"(<c r="D3")",
        "shift reacquire reuse post-noop output should write the later D3 cell");
    check_contains(post_noop_xml, "reacquire-post-noop-reuse",
        "shift reacquire reuse post-noop output should write the later D3 text");
    check_not_contains(post_noop_xml, R"(r="B1")",
        "shift reacquire reuse post-noop output should keep old B1 absent");
    check_not_contains(post_noop_xml, R"(r="A2")",
        "shift reacquire reuse post-noop output should keep old A2 absent");

    check_reopened_shift_output(first_output, "shift reacquire first save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire first save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire first save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire first save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire first save reopened output should read shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire first save reopened output should omit later and old coordinates");
        });
    check_reopened_shift_output(second_output, "shift reacquire second save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire second save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire second save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire second save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire second save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire second save reopened output should keep old coordinates absent");
        });
    check_reopened_shift_output(second_noop_output, "shift reacquire repeat no-op output",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire repeat no-op output should reopen with sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire repeat no-op output should reopen with combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire repeat no-op output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire repeat no-op output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire repeat no-op output should keep old coordinates absent");
        });
    check_reopened_shift_output(post_noop_output, "shift reacquire reuse post-noop output",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire reuse post-noop output should reopen with post-noop sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 4,
                "shift reacquire reuse post-noop output should reopen with post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire reuse post-noop output should keep shifted B1 at C1");
            check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "shift reacquire reuse post-noop output should keep shifted A2");
            check(reopened_sheet.get_cell("D3").text_value() == "reacquire-post-noop-reuse",
                "shift reacquire reuse post-noop output should keep the later edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 4 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "reacquire-post-noop-reuse",
                "shift reacquire reuse post-noop row_cells should expose shifted row order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                reopened_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 3 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "reacquire-post-noop-reuse",
                "shift reacquire reuse post-noop column_cells should expose the later edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire reuse post-noop output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check(sheet.has_pending_changes(),
        "shift reacquire noop save should dirty the borrowed handle before the first save");
    check(sheet.cell_count() == 3,
        "shift reacquire noop save should keep sparse count after the first shift");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire noop save should expose the shifted source row before save");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire noop save pre-save shift");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire noop save first save should clean the original borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire noop save first save should clear dirty materialized diagnostics");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire noop save should return the saved clean materialized session");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "shift reacquire noop save should preserve sparse count on both handles");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire noop save should reuse the saved shifted state instead of reloading source");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire noop save should keep old shifted coordinates absent on both handles");
    const auto check_saved_shift_snapshots =
        [](fastxlsx::WorksheetEditor& snapshot_sheet, std::string_view scenario) {
            const std::string label(scenario);
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                snapshot_sheet.sparse_cells();
            check(all_cells.size() == 3,
                label + " sparse_cells should keep the saved shifted sparse count");
            if (all_cells.size() == 3) {
                check(all_cells[0].reference.row == 1 && all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    label + " sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 && all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    label + " sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 && all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    label + " sparse_cells should expose shifted A2 as A3");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                snapshot_sheet.sparse_cells("A2:B3");
            check(shifted_range.size() == 1 &&
                    shifted_range[0].reference.row == 3 &&
                    shifted_range[0].reference.column == 1 &&
                    shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[0].value.text_value() == "placeholder-a2",
                label + " range sparse_cells should skip old A2 and expose shifted A3");

            const std::array<fastxlsx::WorksheetCellReference, 3> requested_refs {
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 1},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                snapshot_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 2,
                label + " requested sparse_cells should skip the old shifted coordinate");
            if (requested_cells.size() == 2) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 2 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[0].value.number_value() == 1.0,
                    label + " requested sparse_cells should preserve B1 input order");
                check(requested_cells[1].reference.row == 3 &&
                        requested_cells[1].reference.column == 1 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == "placeholder-a2",
                    label + " requested sparse_cells should return shifted A3");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                snapshot_sheet.row_cells(3);
            check(row_three.size() == 1 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2",
                label + " row_cells should expose the shifted row");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                snapshot_sheet.column_cells(1);
            check(column_one.size() == 2 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[0].value.text_value() == "placeholder-a1" &&
                    column_one[1].reference.row == 3 &&
                    column_one[1].reference.column == 1 &&
                    column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[1].value.text_value() == "placeholder-a2",
                label + " column_cells should keep source and shifted rows in order");
        };
    check_saved_shift_snapshots(sheet, "shift reacquire original clean handle");
    check_saved_shift_snapshots(reacquired, "shift reacquire saved clean handle");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("a3", fastxlsx::CellValue::text("invalid-lowercase"));
    }), "shift reacquire max-boundary no-ops should seed diagnostics first");
    check(editor.last_edit_error().has_value(),
        "shift reacquire max-boundary no-ops should expose the seeded diagnostic");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_boundary_noops =
        workbook_editor_public_catalog_snapshot(editor);

    reacquired.insert_rows(1048576, 1);
    reacquired.insert_columns(16384, 1);
    reacquired.delete_rows(1048576, 1);
    reacquired.delete_columns(16384, 1);
    check(!editor.last_edit_error().has_value(),
        "shift reacquire max-boundary no-ops should clear prior diagnostics");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire max-boundary no-ops should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire max-boundary no-ops should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire max-boundary no-ops should keep dirty materialized diagnostics clear");
    check(sheet.cell_count() == 3 && reacquired.cell_count() == 3,
        "shift reacquire max-boundary no-ops should preserve saved sparse count");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire max-boundary no-ops should preserve the saved shifted row");
    check(!sheet.try_cell("A1048576").has_value() &&
            !reacquired.try_cell("XFD1").has_value(),
        "shift reacquire max-boundary no-ops should not synthesize edge cells");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_boundary_noops,
        "shift reacquire max-boundary no-ops");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "shift reacquire noop output should match the first save");
    check_reopened_shift_output(noop_output, "shift reacquire noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire noop save reopened output should omit later and old coordinates");
            check_saved_shift_snapshots(reopened_sheet,
                "shift reacquire noop save reopened output");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire repeat noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire repeat noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire repeat noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire repeat noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire repeat noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "shift reacquire repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire repeat noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire repeat noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire repeat noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire repeat noop save reopened output should omit later and old coordinates");
        });

    reacquired.set_cell("C3", fastxlsx::CellValue::text("post-noop-shift"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "shift reacquire post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "shift reacquire post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 3,
        "shift reacquire post-noop edit should expand bounds to C3");
    const fastxlsx::CellValue post_noop_cell = reacquired.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-shift",
        "shift reacquire post-noop edit should expose the new dirty cell");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "shift reacquire post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "shift reacquire post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output, "shift reacquire post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire post-noop save reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 4,
                "shift reacquire post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 4) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    "shift reacquire post-noop save reopened sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    "shift reacquire post-noop save reopened sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 &&
                        all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    "shift reacquire post-noop save reopened sparse_cells should keep shifted A3 third");
                check(all_cells[3].reference.row == 3 &&
                        all_cells[3].reference.column == 3 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[3].value.text_value() == "post-noop-shift",
                    "shift reacquire post-noop save reopened sparse_cells should keep post-noop C3 fourth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A2:C3");
            check(shifted_range.size() == 2 &&
                    shifted_range[0].reference.row == 3 &&
                    shifted_range[0].reference.column == 1 &&
                    shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[0].value.text_value() == "placeholder-a2" &&
                    shifted_range[1].reference.row == 3 &&
                    shifted_range[1].reference.column == 3 &&
                    shifted_range[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[1].value.text_value() == "post-noop-shift",
                "shift reacquire post-noop save reopened range sparse_cells should expose shifted row and post-noop edit");
            const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 3,
                "shift reacquire post-noop save reopened requested sparse_cells should skip old A2");
            if (requested_cells.size() == 3) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 2 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[0].value.number_value() == 1.0,
                    "shift reacquire post-noop save reopened requested sparse_cells should keep B1 input order");
                check(requested_cells[1].reference.row == 3 &&
                        requested_cells[1].reference.column == 1 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == "placeholder-a2",
                    "shift reacquire post-noop save reopened requested sparse_cells should return shifted A3");
                check(requested_cells[2].reference.row == 3 &&
                        requested_cells[2].reference.column == 3 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "post-noop-shift",
                    "shift reacquire post-noop save reopened requested sparse_cells should return post-noop C3");
            }
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire post-noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-shift",
                "shift reacquire post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire post-noop save reopened output should omit later and old coordinates");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                reopened_sheet.row_cells(3);
            check(reopened_row_three.size() == 2 &&
                    reopened_row_three[0].reference.row == 3 &&
                    reopened_row_three[0].reference.column == 1 &&
                    reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_three[1].reference.row == 3 &&
                    reopened_row_three[1].reference.column == 3 &&
                    reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[1].value.text_value() == "post-noop-shift",
                "shift reacquire post-noop save reopened row_cells should expose shifted row and post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 3 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_three[0].value.text_value() == "post-noop-shift",
                "shift reacquire post-noop save reopened column_cells should expose post-noop edit");
        });
}

void test_public_worksheet_editor_insert_rows_reacquire_formula_noop_save_preserves_saved_session()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-reacquire-formula-noop-source.xlsx");
    const std::filesystem::path first_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-reacquire-formula-noop-first-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-reacquire-formula-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-reacquire-formula-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-insert-rows-reacquire-formula-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(2, 3, fastxlsx::CellValue::formula("A1+B1"));
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("tail-c3"));
    sheet.insert_rows(2, 1);

    check(sheet.has_pending_changes(),
        "insert_rows formula reacquire noop save should dirty the borrowed handle before save");
    check(sheet.cell_count() == 5,
        "insert_rows formula reacquire noop save should preserve the shifted sparse count");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("C3");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula.text_value() == "A1+B1",
        "insert_rows formula reacquire noop save should translate the moved formula before save");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "insert_rows formula reacquire noop save should expose shifted source A2 before save");
    check(sheet.get_cell("C4").text_value() == "tail-c3",
        "insert_rows formula reacquire noop save should expose shifted dirty C3 before save");
    check(!sheet.try_cell("C2").has_value(),
        "insert_rows formula reacquire noop save should keep the old formula coordinate absent");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "insert_rows formula reacquire noop save pre-save shift");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "insert_rows formula reacquire noop save first save should clean the original handle");
    check(editor.pending_change_count() == 1,
        "insert_rows formula reacquire noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_rows formula reacquire noop save first save should clear dirty materialized diagnostics");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "insert_rows formula reacquire noop save should return the saved clean session");
    check(sheet.cell_count() == 5 && reacquired.cell_count() == 5,
        "insert_rows formula reacquire noop save should preserve sparse count on both handles");
    check(sheet.get_cell("C3").text_value() == "A1+B1" &&
            reacquired.get_cell("C3").text_value() == "A1+B1",
        "insert_rows formula reacquire noop save should reuse the saved translated formula");
    check(sheet.get_cell("C4").text_value() == "tail-c3" &&
            reacquired.get_cell("C4").text_value() == "tail-c3",
        "insert_rows formula reacquire noop save should reuse the saved shifted dirty cell");
    check(!sheet.try_cell("C2").has_value() && !reacquired.try_cell("C2").has_value(),
        "insert_rows formula reacquire noop save should keep old formula coordinates absent");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string& first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:C4"/>)",
        "insert_rows formula reacquire noop first save should project shifted bounds");
    check_contains(first_worksheet_xml, R"(<c r="C3"><f>A1+B1</f></c>)",
        "insert_rows formula reacquire noop first save should write the translated formula");
    check_contains(first_worksheet_xml, "tail-c3",
        "insert_rows formula reacquire noop first save should write the shifted dirty tail");
    check_not_contains(first_worksheet_xml, R"(r="C2")",
        "insert_rows formula reacquire noop first save should omit the old formula coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "insert_rows formula reacquire noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "insert_rows formula reacquire noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_rows formula reacquire noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "insert_rows formula reacquire noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_rows formula reacquire noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "insert_rows formula reacquire noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "insert_rows formula reacquire noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "insert_rows formula reacquire noop output should match the first save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows formula reacquire noop save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "insert_rows formula reacquire noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "insert_rows formula reacquire noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 3,
                "insert_rows formula reacquire noop save reopened output should expose shifted bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 5,
                "insert_rows formula reacquire noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 5) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    "insert_rows formula reacquire noop save reopened sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    "insert_rows formula reacquire noop save reopened sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 &&
                        all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    "insert_rows formula reacquire noop save reopened sparse_cells should keep shifted A3 third");
                check(all_cells[3].reference.row == 3 &&
                        all_cells[3].reference.column == 3 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                        all_cells[3].value.text_value() == "A1+B1",
                    "insert_rows formula reacquire noop save reopened sparse_cells should keep translated C3 fourth");
                check(all_cells[4].reference.row == 4 &&
                        all_cells[4].reference.column == 3 &&
                        all_cells[4].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[4].value.text_value() == "tail-c3",
                    "insert_rows formula reacquire noop save reopened sparse_cells should keep shifted C4 fifth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 3 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                    row_three[1].value.text_value() == "A1+B1",
                "insert_rows formula reacquire noop save reopened row_cells should expose shifted source and formula");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 2 &&
                    column_three[0].reference.row == 3 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    column_three[0].value.text_value() == "A1+B1" &&
                    column_three[1].reference.row == 4 &&
                    column_three[1].reference.column == 3 &&
                    column_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[1].value.text_value() == "tail-c3",
                "insert_rows formula reacquire noop save reopened column_cells should expose formula and shifted tail");
            check(!reopened_sheet.try_cell("C2").has_value(),
                "insert_rows formula reacquire noop save reopened output should keep old formula coordinate absent");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "insert_rows formula reacquire repeat noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "insert_rows formula reacquire repeat noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_rows formula reacquire repeat noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "insert_rows formula reacquire repeat noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_rows formula reacquire repeat noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "insert_rows formula reacquire repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "insert_rows formula reacquire repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows formula reacquire repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "insert_rows formula reacquire repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_rows formula reacquire repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "insert_rows formula reacquire repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output,
        "insert_rows formula reacquire repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "insert_rows formula reacquire repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 3,
                "insert_rows formula reacquire repeat noop save reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_formula = reopened_sheet.get_cell("C3");
            check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_formula.text_value() == "A1+B1",
                "insert_rows formula reacquire repeat noop save reopened output should keep translated formula");
            const fastxlsx::CellValue reopened_tail = reopened_sheet.get_cell("C4");
            check(reopened_tail.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_tail.text_value() == "tail-c3",
                "insert_rows formula reacquire repeat noop save reopened output should keep shifted dirty tail");
            check(!reopened_sheet.try_cell("C2").has_value(),
                "insert_rows formula reacquire repeat noop save reopened output should keep old formula coordinate absent");
        });

    reacquired.set_cell("D4", fastxlsx::CellValue::text("post-noop-insert-rows-formula"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "insert_rows formula reacquire post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 6 && reacquired.cell_count() == 6,
        "insert_rows formula reacquire post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "insert_rows formula reacquire post-noop edit should expand bounds to D4");
    const fastxlsx::CellValue post_noop_cell = reacquired.get_cell("D4");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-insert-rows-formula",
        "insert_rows formula reacquire post-noop edit should expose the new dirty cell");
    const fastxlsx::CellValue post_noop_formula = reacquired.get_cell("C3");
    check(post_noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_formula.text_value() == "A1+B1",
        "insert_rows formula reacquire post-noop edit should keep the translated formula");
    const fastxlsx::CellValue post_noop_tail = reacquired.get_cell("C4");
    check(post_noop_tail.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_tail.text_value() == "tail-c3",
        "insert_rows formula reacquire post-noop edit should keep the shifted dirty tail");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "insert_rows formula reacquire post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "insert_rows formula reacquire post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "insert_rows formula reacquire post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "insert_rows formula reacquire post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_rows formula reacquire post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "insert_rows formula reacquire post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_rows formula reacquire post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_rows formula reacquire post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "insert_rows formula reacquire post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_rows formula reacquire post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "insert_rows formula reacquire post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output,
        "insert_rows formula reacquire post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "insert_rows formula reacquire post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 4,
                "insert_rows formula reacquire post-noop save reopened output should expose post-noop bounds");

            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 6,
                "insert_rows formula reacquire post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 6) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    "insert_rows formula reacquire post-noop save reopened sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    "insert_rows formula reacquire post-noop save reopened sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 &&
                        all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    "insert_rows formula reacquire post-noop save reopened sparse_cells should keep shifted A3 third");
                check(all_cells[3].reference.row == 3 &&
                        all_cells[3].reference.column == 3 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                        all_cells[3].value.text_value() == "A1+B1",
                    "insert_rows formula reacquire post-noop save reopened sparse_cells should keep translated C3 fourth");
                check(all_cells[4].reference.row == 4 &&
                        all_cells[4].reference.column == 3 &&
                        all_cells[4].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[4].value.text_value() == "tail-c3",
                    "insert_rows formula reacquire post-noop save reopened sparse_cells should keep shifted C4 fifth");
                check(all_cells[5].reference.row == 4 &&
                        all_cells[5].reference.column == 4 &&
                        all_cells[5].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[5].value.text_value() == "post-noop-insert-rows-formula",
                    "insert_rows formula reacquire post-noop save reopened sparse_cells should keep post-noop D4 sixth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A1:D4");
            check(shifted_range.size() == 6,
                "insert_rows formula reacquire post-noop save reopened range sparse_cells should expose all records");
            const std::array<fastxlsx::WorksheetCellReference, 7> requested_refs {
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::WorksheetCellReference {3, 3},
                fastxlsx::WorksheetCellReference {4, 3},
                fastxlsx::WorksheetCellReference {4, 4},
                fastxlsx::WorksheetCellReference {2, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 6,
                "insert_rows formula reacquire post-noop save reopened requested sparse_cells should skip old C2");
            if (requested_cells.size() == 6) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 1 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[0].value.text_value() == "placeholder-a1",
                    "insert_rows formula reacquire post-noop save reopened requested sparse_cells should keep A1 first");
                check(requested_cells[1].reference.row == 1 &&
                        requested_cells[1].reference.column == 2 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[1].value.number_value() == 1.0,
                    "insert_rows formula reacquire post-noop save reopened requested sparse_cells should keep B1 second");
                check(requested_cells[2].reference.row == 3 &&
                        requested_cells[2].reference.column == 1 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "placeholder-a2",
                    "insert_rows formula reacquire post-noop save reopened requested sparse_cells should keep A3 third");
                check(requested_cells[3].reference.row == 3 &&
                        requested_cells[3].reference.column == 3 &&
                        requested_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                        requested_cells[3].value.text_value() == "A1+B1",
                    "insert_rows formula reacquire post-noop save reopened requested sparse_cells should keep C3 fourth");
                check(requested_cells[4].reference.row == 4 &&
                        requested_cells[4].reference.column == 3 &&
                        requested_cells[4].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[4].value.text_value() == "tail-c3",
                    "insert_rows formula reacquire post-noop save reopened requested sparse_cells should keep C4 fifth");
                check(requested_cells[5].reference.row == 4 &&
                        requested_cells[5].reference.column == 4 &&
                        requested_cells[5].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[5].value.text_value() == "post-noop-insert-rows-formula",
                    "insert_rows formula reacquire post-noop save reopened requested sparse_cells should keep D4 sixth");
            }

            const fastxlsx::CellValue reopened_formula = reopened_sheet.get_cell("C3");
            check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_formula.text_value() == "A1+B1",
                "insert_rows formula reacquire post-noop save reopened output should keep translated formula");
            const fastxlsx::CellValue reopened_tail = reopened_sheet.get_cell("C4");
            check(reopened_tail.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_tail.text_value() == "tail-c3",
                "insert_rows formula reacquire post-noop save reopened output should keep shifted dirty tail");
            const fastxlsx::CellValue reopened_post_noop = reopened_sheet.get_cell("D4");
            check(reopened_post_noop.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_post_noop.text_value() == "post-noop-insert-rows-formula",
                "insert_rows formula reacquire post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("C2").has_value(),
                "insert_rows formula reacquire post-noop save reopened output should keep old formula coordinate absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_four =
                reopened_sheet.row_cells(4);
            check(row_four.size() == 2 &&
                    row_four[0].reference.row == 4 &&
                    row_four[0].reference.column == 3 &&
                    row_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_four[0].value.text_value() == "tail-c3" &&
                    row_four[1].reference.row == 4 &&
                    row_four[1].reference.column == 4 &&
                    row_four[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_four[1].value.text_value() == "post-noop-insert-rows-formula",
                "insert_rows formula reacquire post-noop save reopened row_cells should expose shifted tail and D4");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                reopened_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 4 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "post-noop-insert-rows-formula",
                "insert_rows formula reacquire post-noop save reopened column_cells should expose post-noop D4");
        });
}

void test_public_worksheet_editor_delete_columns_reacquire_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-delete-columns-reacquire-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-reacquire-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-reacquire-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-reacquire-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-reacquire-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(1, 3, fastxlsx::CellValue::formula("B2+D1"));
    sheet.set_cell(2, 4, fastxlsx::CellValue::text("tail-d2"));
    sheet.delete_columns(1, 1);
    check(sheet.has_pending_changes(),
        "delete_columns reacquire noop save should dirty the borrowed handle before the first save");
    check(sheet.cell_count() == 3,
        "delete_columns reacquire noop save should keep sparse count after the first shift");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("B1");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula.text_value() == "A2+C1",
        "delete_columns reacquire noop save should expose the translated formula before save");
    check(sheet.get_cell("C2").text_value() == "tail-d2",
        "delete_columns reacquire noop save should expose the shifted dirty cell before save");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "delete_columns reacquire noop save pre-save shift");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "delete_columns reacquire noop save first save should clean the original borrowed handle");
    check(editor.pending_change_count() == 1,
        "delete_columns reacquire noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_columns reacquire noop save first save should clear dirty materialized diagnostics");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "delete_columns reacquire noop save should return the saved clean materialized session");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "delete_columns reacquire noop save should preserve sparse count on both handles");
    check(reacquired.get_cell("A1").number_value() == 1.0 &&
            sheet.get_cell("A1").number_value() == 1.0,
        "delete_columns reacquire noop save should reuse the saved shifted source cell");
    check(reacquired.get_cell("B1").text_value() == "A2+C1" &&
            sheet.get_cell("B1").text_value() == "A2+C1",
        "delete_columns reacquire noop save should reuse the saved translated formula");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value() &&
            !reacquired.try_cell("C1").has_value() && !sheet.try_cell("C1").has_value(),
        "delete_columns reacquire noop save should keep deleted and old coordinates absent on both handles");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "delete_columns reacquire noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "delete_columns reacquire noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_columns reacquire noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "delete_columns reacquire noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns reacquire noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "delete_columns reacquire noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "delete_columns reacquire noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "delete_columns reacquire noop output should match the first save");
    check_reopened_shift_output(noop_output, "delete_columns reacquire noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "delete_columns reacquire noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "delete_columns reacquire noop save reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "delete_columns reacquire noop save reopened output should keep shifted source B1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_b1.text_value() == "A2+C1",
                "delete_columns reacquire noop save reopened output should keep translated formula");
            const fastxlsx::CellValue reopened_c2 = reopened_sheet.get_cell("C2");
            check(reopened_c2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c2.text_value() == "tail-d2",
                "delete_columns reacquire noop save reopened output should keep shifted dirty cell");
            check(!reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("D2").has_value(),
                "delete_columns reacquire noop save reopened output should keep deleted and old coordinates absent");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "delete_columns reacquire repeat noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "delete_columns reacquire repeat noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_columns reacquire repeat noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "delete_columns reacquire repeat noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns reacquire repeat noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "delete_columns reacquire repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "delete_columns reacquire repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns reacquire repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "delete_columns reacquire repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_columns reacquire repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "delete_columns reacquire repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "delete_columns reacquire repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "delete_columns reacquire repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "delete_columns reacquire repeat noop save reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "delete_columns reacquire repeat noop save reopened output should keep shifted source B1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_b1.text_value() == "A2+C1",
                "delete_columns reacquire repeat noop save reopened output should keep translated formula");
            const fastxlsx::CellValue reopened_c2 = reopened_sheet.get_cell("C2");
            check(reopened_c2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c2.text_value() == "tail-d2",
                "delete_columns reacquire repeat noop save reopened output should keep shifted dirty cell");
            check(!reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("D2").has_value(),
                "delete_columns reacquire repeat noop save reopened output should keep deleted and old coordinates absent");
        });

    reacquired.set_cell("D2", fastxlsx::CellValue::text("post-noop-delete-columns"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "delete_columns reacquire post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "delete_columns reacquire post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 2, 4,
        "delete_columns reacquire post-noop edit should expand bounds to D2");
    const fastxlsx::CellValue post_noop_cell = reacquired.get_cell("D2");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-delete-columns",
        "delete_columns reacquire post-noop edit should expose the new dirty cell");
    const fastxlsx::CellValue post_noop_formula = reacquired.get_cell("B1");
    check(post_noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_formula.text_value() == "A2+C1",
        "delete_columns reacquire post-noop edit should keep the translated formula");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "delete_columns reacquire post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "delete_columns reacquire post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "delete_columns reacquire post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "delete_columns reacquire post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_columns reacquire post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "delete_columns reacquire post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns reacquire post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "delete_columns reacquire post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_columns reacquire post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "delete_columns reacquire post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output, "delete_columns reacquire post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "delete_columns reacquire post-noop save reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 4,
                "delete_columns reacquire post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 4) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[0].value.number_value() == 1.0,
                    "delete_columns reacquire post-noop save reopened sparse_cells should keep shifted A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                        all_cells[1].value.text_value() == "A2+C1",
                    "delete_columns reacquire post-noop save reopened sparse_cells should keep translated B1 second");
                check(all_cells[2].reference.row == 2 &&
                        all_cells[2].reference.column == 3 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "tail-d2",
                    "delete_columns reacquire post-noop save reopened sparse_cells should keep shifted C2 third");
                check(all_cells[3].reference.row == 2 &&
                        all_cells[3].reference.column == 4 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[3].value.text_value() == "post-noop-delete-columns",
                    "delete_columns reacquire post-noop save reopened sparse_cells should keep post-noop D2 fourth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A1:D2");
            check(shifted_range.size() == 4,
                "delete_columns reacquire post-noop save reopened range sparse_cells should expose all records");
            const std::array<fastxlsx::WorksheetCellReference, 5> requested_refs {
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {2, 3},
                fastxlsx::WorksheetCellReference {2, 4},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 4,
                "delete_columns reacquire post-noop save reopened requested sparse_cells should skip deleted A2");
            if (requested_cells.size() == 4) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 1 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[0].value.number_value() == 1.0,
                    "delete_columns reacquire post-noop save reopened requested sparse_cells should keep A1 first");
                check(requested_cells[1].reference.row == 1 &&
                        requested_cells[1].reference.column == 2 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                        requested_cells[1].value.text_value() == "A2+C1",
                    "delete_columns reacquire post-noop save reopened requested sparse_cells should keep B1 second");
                check(requested_cells[2].reference.row == 2 &&
                        requested_cells[2].reference.column == 3 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "tail-d2",
                    "delete_columns reacquire post-noop save reopened requested sparse_cells should keep C2 third");
                check(requested_cells[3].reference.row == 2 &&
                        requested_cells[3].reference.column == 4 &&
                        requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[3].value.text_value() == "post-noop-delete-columns",
                    "delete_columns reacquire post-noop save reopened requested sparse_cells should keep D2 fourth");
            }
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "delete_columns reacquire post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "delete_columns reacquire post-noop save reopened output should keep shifted source B1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_b1.text_value() == "A2+C1",
                "delete_columns reacquire post-noop save reopened output should keep translated formula");
            const fastxlsx::CellValue reopened_c2 = reopened_sheet.get_cell("C2");
            check(reopened_c2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c2.text_value() == "tail-d2",
                "delete_columns reacquire post-noop save reopened output should keep shifted dirty cell");
            const fastxlsx::CellValue reopened_d2 = reopened_sheet.get_cell("D2");
            check(reopened_d2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d2.text_value() == "post-noop-delete-columns",
                "delete_columns reacquire post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("C1").has_value(),
                "delete_columns reacquire post-noop save reopened output should keep deleted and old coordinates absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                reopened_sheet.row_cells(1);
            check(reopened_row_one.size() == 2 &&
                    reopened_row_one[0].reference.row == 1 &&
                    reopened_row_one[0].reference.column == 1 &&
                    reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_row_one[0].value.number_value() == 1.0 &&
                    reopened_row_one[1].reference.row == 1 &&
                    reopened_row_one[1].reference.column == 2 &&
                    reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_one[1].value.text_value() == "A2+C1",
                "delete_columns reacquire post-noop save reopened row_cells should expose shifted A1 and formula B1");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 2 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 3 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "tail-d2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 4 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[1].value.text_value() == "post-noop-delete-columns",
                "delete_columns reacquire post-noop save reopened row_cells should expose shifted C2 and post-noop D2");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 2 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_four[0].value.text_value() == "post-noop-delete-columns",
                "delete_columns reacquire post-noop save reopened column_cells should expose post-noop D2");
        });
}

void test_public_worksheet_editor_delete_rows_reacquire_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-delete-rows-reacquire-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-reacquire-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-reacquire-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-reacquire-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-reacquire-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 3, fastxlsx::CellValue::formula("A2+B4"));
    sheet.set_cell(4, 2, fastxlsx::CellValue::text("tail-b4"));
    sheet.delete_rows(1, 1);
    check(sheet.has_pending_changes(),
        "delete_rows reacquire noop save should dirty the borrowed handle before the first save");
    check(sheet.cell_count() == 3,
        "delete_rows reacquire noop save should keep sparse count after the first shift");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2",
        "delete_rows reacquire noop save should expose the shifted source row before save");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("C3");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula.text_value() == "A1+B3",
        "delete_rows reacquire noop save should expose the translated formula before save");
    check(sheet.get_cell("B3").text_value() == "tail-b4",
        "delete_rows reacquire noop save should expose the shifted dirty cell before save");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "delete_rows reacquire noop save pre-save shift");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "delete_rows reacquire noop save first save should clean the original borrowed handle");
    check(editor.pending_change_count() == 1,
        "delete_rows reacquire noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_rows reacquire noop save first save should clear dirty materialized diagnostics");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "delete_rows reacquire noop save should return the saved clean materialized session");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "delete_rows reacquire noop save should preserve sparse count on both handles");
    check(reacquired.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("A1").text_value() == "placeholder-a2",
        "delete_rows reacquire noop save should reuse the saved shifted source row");
    check(reacquired.get_cell("B3").text_value() == "tail-b4" &&
            sheet.get_cell("B3").text_value() == "tail-b4",
        "delete_rows reacquire noop save should reuse the saved shifted dirty cell");
    const fastxlsx::CellValue reacquired_formula = reacquired.get_cell("C3");
    check(reacquired_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_formula.text_value() == "A1+B3" &&
            sheet.get_cell("C3").text_value() == "A1+B3",
        "delete_rows reacquire noop save should reuse the saved translated formula");
    check(!reacquired.try_cell("B1").has_value() && !sheet.try_cell("B1").has_value() &&
            !reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value() &&
            !reacquired.try_cell("B4").has_value() && !sheet.try_cell("B4").has_value() &&
            !reacquired.try_cell("C4").has_value() && !sheet.try_cell("C4").has_value(),
        "delete_rows reacquire noop save should keep deleted and old coordinates absent on both handles");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "delete_rows reacquire noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "delete_rows reacquire noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_rows reacquire noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "delete_rows reacquire noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows reacquire noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "delete_rows reacquire noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "delete_rows reacquire noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "delete_rows reacquire noop output should match the first save");
    check_reopened_shift_output(noop_output, "delete_rows reacquire noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "delete_rows reacquire noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "delete_rows reacquire noop save reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a2",
                "delete_rows reacquire noop save reopened output should keep shifted source A2");
            const fastxlsx::CellValue reopened_b3 = reopened_sheet.get_cell("B3");
            check(reopened_b3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b3.text_value() == "tail-b4",
                "delete_rows reacquire noop save reopened output should keep shifted dirty cell");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c3.text_value() == "A1+B3",
                "delete_rows reacquire noop save reopened output should keep translated formula");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("B4").has_value() &&
                    !reopened_sheet.try_cell("C4").has_value(),
                "delete_rows reacquire noop save reopened output should keep deleted and old coordinates absent");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "delete_rows reacquire repeat noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "delete_rows reacquire repeat noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_rows reacquire repeat noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "delete_rows reacquire repeat noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows reacquire repeat noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "delete_rows reacquire repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "delete_rows reacquire repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows reacquire repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "delete_rows reacquire repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_rows reacquire repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "delete_rows reacquire repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "delete_rows reacquire repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "delete_rows reacquire repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "delete_rows reacquire repeat noop save reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a2",
                "delete_rows reacquire repeat noop save reopened output should keep shifted source A2");
            const fastxlsx::CellValue reopened_b3 = reopened_sheet.get_cell("B3");
            check(reopened_b3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b3.text_value() == "tail-b4",
                "delete_rows reacquire repeat noop save reopened output should keep shifted dirty cell");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c3.text_value() == "A1+B3",
                "delete_rows reacquire repeat noop save reopened output should keep translated formula");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("B4").has_value() &&
                    !reopened_sheet.try_cell("C4").has_value(),
                "delete_rows reacquire repeat noop save reopened output should keep deleted and old coordinates absent");
        });

    reacquired.set_cell("D3", fastxlsx::CellValue::text("post-noop-delete-rows"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "delete_rows reacquire post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "delete_rows reacquire post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 4,
        "delete_rows reacquire post-noop edit should expand bounds to D3");
    const fastxlsx::CellValue post_noop_cell = reacquired.get_cell("D3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-delete-rows",
        "delete_rows reacquire post-noop edit should expose the new dirty cell");
    const fastxlsx::CellValue post_noop_formula = reacquired.get_cell("C3");
    check(post_noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_formula.text_value() == "A1+B3",
        "delete_rows reacquire post-noop edit should keep the translated formula");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "delete_rows reacquire post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "delete_rows reacquire post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "delete_rows reacquire post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "delete_rows reacquire post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_rows reacquire post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "delete_rows reacquire post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows reacquire post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "delete_rows reacquire post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_rows reacquire post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "delete_rows reacquire post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output, "delete_rows reacquire post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "delete_rows reacquire post-noop save reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 4,
                "delete_rows reacquire post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 4) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a2",
                    "delete_rows reacquire post-noop save reopened sparse_cells should keep shifted A1 first");
                check(all_cells[1].reference.row == 3 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[1].value.text_value() == "tail-b4",
                    "delete_rows reacquire post-noop save reopened sparse_cells should keep shifted B3 second");
                check(all_cells[2].reference.row == 3 &&
                        all_cells[2].reference.column == 3 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                        all_cells[2].value.text_value() == "A1+B3",
                    "delete_rows reacquire post-noop save reopened sparse_cells should keep translated C3 third");
                check(all_cells[3].reference.row == 3 &&
                        all_cells[3].reference.column == 4 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[3].value.text_value() == "post-noop-delete-rows",
                    "delete_rows reacquire post-noop save reopened sparse_cells should keep post-noop D3 fourth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A1:D3");
            check(shifted_range.size() == 4,
                "delete_rows reacquire post-noop save reopened range sparse_cells should expose all records");
            const std::array<fastxlsx::WorksheetCellReference, 5> requested_refs {
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 2},
                fastxlsx::WorksheetCellReference {3, 3},
                fastxlsx::WorksheetCellReference {3, 4},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 4,
                "delete_rows reacquire post-noop save reopened requested sparse_cells should skip old A2");
            if (requested_cells.size() == 4) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 1 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[0].value.text_value() == "placeholder-a2",
                    "delete_rows reacquire post-noop save reopened requested sparse_cells should keep A1 first");
                check(requested_cells[1].reference.row == 3 &&
                        requested_cells[1].reference.column == 2 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == "tail-b4",
                    "delete_rows reacquire post-noop save reopened requested sparse_cells should keep B3 second");
                check(requested_cells[2].reference.row == 3 &&
                        requested_cells[2].reference.column == 3 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                        requested_cells[2].value.text_value() == "A1+B3",
                    "delete_rows reacquire post-noop save reopened requested sparse_cells should keep C3 third");
                check(requested_cells[3].reference.row == 3 &&
                        requested_cells[3].reference.column == 4 &&
                        requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[3].value.text_value() == "post-noop-delete-rows",
                    "delete_rows reacquire post-noop save reopened requested sparse_cells should keep D3 fourth");
            }
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 4,
                "delete_rows reacquire post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a2",
                "delete_rows reacquire post-noop save reopened output should keep shifted source A2");
            const fastxlsx::CellValue reopened_b3 = reopened_sheet.get_cell("B3");
            check(reopened_b3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b3.text_value() == "tail-b4",
                "delete_rows reacquire post-noop save reopened output should keep shifted dirty cell");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c3.text_value() == "A1+B3",
                "delete_rows reacquire post-noop save reopened output should keep translated formula");
            const fastxlsx::CellValue reopened_d3 = reopened_sheet.get_cell("D3");
            check(reopened_d3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d3.text_value() == "post-noop-delete-rows",
                "delete_rows reacquire post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("B4").has_value() &&
                    !reopened_sheet.try_cell("C4").has_value(),
                "delete_rows reacquire post-noop save reopened output should keep deleted and old coordinates absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                reopened_sheet.row_cells(3);
            check(reopened_row_three.size() == 3 &&
                    reopened_row_three[0].reference.row == 3 &&
                    reopened_row_three[0].reference.column == 2 &&
                    reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[0].value.text_value() == "tail-b4" &&
                    reopened_row_three[1].reference.row == 3 &&
                    reopened_row_three[1].reference.column == 3 &&
                    reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_three[1].value.text_value() == "A1+B3" &&
                    reopened_row_three[2].reference.row == 3 &&
                    reopened_row_three[2].reference.column == 4 &&
                    reopened_row_three[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[2].value.text_value() == "post-noop-delete-rows",
                "delete_rows reacquire post-noop save reopened row_cells should expose shifted row and post-noop D3");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 3 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_four[0].value.text_value() == "post-noop-delete-rows",
                "delete_rows reacquire post-noop save reopened column_cells should expose post-noop D3");
        });
}

void test_public_worksheet_editor_insert_columns_reacquire_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-insert-columns-reacquire-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-reacquire-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-reacquire-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-reacquire-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-insert-columns-reacquire-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(2, 3, fastxlsx::CellValue::formula("A1+B1"));
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("extra-c3"));
    sheet.insert_columns(2, 2);
    check(sheet.has_pending_changes(),
        "insert_columns reacquire noop save should dirty the borrowed handle before the first save");
    check(sheet.cell_count() == 5,
        "insert_columns reacquire noop save should keep sparse count after the first shift");
    check(sheet.get_cell("D1").number_value() == 1.0,
        "insert_columns reacquire noop save should expose the shifted source cell before save");
    const fastxlsx::CellValue shifted_formula = sheet.get_cell("E2");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula.text_value() == "A1+D1",
        "insert_columns reacquire noop save should expose the translated formula before save");
    check(sheet.get_cell("E3").text_value() == "extra-c3",
        "insert_columns reacquire noop save should expose the shifted dirty cell before save");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "insert_columns reacquire noop save pre-save shift");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "insert_columns reacquire noop save first save should clean the original borrowed handle");
    check(editor.pending_change_count() == 1,
        "insert_columns reacquire noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns reacquire noop save first save should clear dirty materialized diagnostics");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "insert_columns reacquire noop save should return the saved clean materialized session");
    check(reacquired.cell_count() == 5 && sheet.cell_count() == 5,
        "insert_columns reacquire noop save should preserve sparse count on both handles");
    check(reacquired.get_cell("D1").number_value() == 1.0 &&
            sheet.get_cell("D1").number_value() == 1.0,
        "insert_columns reacquire noop save should reuse the saved shifted source cell");
    check(reacquired.get_cell("E3").text_value() == "extra-c3" &&
            sheet.get_cell("E3").text_value() == "extra-c3",
        "insert_columns reacquire noop save should reuse the saved shifted dirty cell");
    const fastxlsx::CellValue reacquired_formula = reacquired.get_cell("E2");
    check(reacquired_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_formula.text_value() == "A1+D1" &&
            sheet.get_cell("E2").text_value() == "A1+D1",
        "insert_columns reacquire noop save should reuse the saved translated formula");
    check(!reacquired.try_cell("B1").has_value() && !sheet.try_cell("B1").has_value() &&
            !reacquired.try_cell("C2").has_value() && !sheet.try_cell("C2").has_value() &&
            !reacquired.try_cell("C3").has_value() && !sheet.try_cell("C3").has_value(),
        "insert_columns reacquire noop save should keep inserted and old coordinates absent on both handles");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "insert_columns reacquire noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "insert_columns reacquire noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns reacquire noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "insert_columns reacquire noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns reacquire noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "insert_columns reacquire noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "insert_columns reacquire noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "insert_columns reacquire noop output should match the first save");
    check_reopened_shift_output(noop_output, "insert_columns reacquire noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "insert_columns reacquire noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 5,
                "insert_columns reacquire noop save reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a1",
                "insert_columns reacquire noop save reopened output should keep source A1");
            const fastxlsx::CellValue reopened_d1 = reopened_sheet.get_cell("D1");
            check(reopened_d1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1.number_value() == 1.0,
                "insert_columns reacquire noop save reopened output should keep shifted source B1");
            const fastxlsx::CellValue reopened_e2 = reopened_sheet.get_cell("E2");
            check(reopened_e2.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_e2.text_value() == "A1+D1",
                "insert_columns reacquire noop save reopened output should keep translated formula");
            const fastxlsx::CellValue reopened_e3 = reopened_sheet.get_cell("E3");
            check(reopened_e3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e3.text_value() == "extra-c3",
                "insert_columns reacquire noop save reopened output should keep shifted dirty cell");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "insert_columns reacquire noop save reopened output should keep inserted and old coordinates absent");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "insert_columns reacquire repeat noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "insert_columns reacquire repeat noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns reacquire repeat noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "insert_columns reacquire repeat noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns reacquire repeat noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "insert_columns reacquire repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "insert_columns reacquire repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "insert_columns reacquire repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "insert_columns reacquire repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_columns reacquire repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "insert_columns reacquire repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "insert_columns reacquire repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "insert_columns reacquire repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 5,
                "insert_columns reacquire repeat noop save reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a1",
                "insert_columns reacquire repeat noop save reopened output should keep source A1");
            const fastxlsx::CellValue reopened_d1 = reopened_sheet.get_cell("D1");
            check(reopened_d1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1.number_value() == 1.0,
                "insert_columns reacquire repeat noop save reopened output should keep shifted source B1");
            const fastxlsx::CellValue reopened_e2 = reopened_sheet.get_cell("E2");
            check(reopened_e2.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_e2.text_value() == "A1+D1",
                "insert_columns reacquire repeat noop save reopened output should keep translated formula");
            const fastxlsx::CellValue reopened_e3 = reopened_sheet.get_cell("E3");
            check(reopened_e3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e3.text_value() == "extra-c3",
                "insert_columns reacquire repeat noop save reopened output should keep shifted dirty cell");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "insert_columns reacquire repeat noop save reopened output should keep inserted and old coordinates absent");
        });

    reacquired.set_cell("F3", fastxlsx::CellValue::text("post-noop-insert-columns"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "insert_columns reacquire post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 6 && reacquired.cell_count() == 6,
        "insert_columns reacquire post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 6,
        "insert_columns reacquire post-noop edit should expand bounds to F3");
    const fastxlsx::CellValue post_noop_cell = reacquired.get_cell("F3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-insert-columns",
        "insert_columns reacquire post-noop edit should expose the new dirty cell");
    const fastxlsx::CellValue post_noop_formula = reacquired.get_cell("E2");
    check(post_noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_formula.text_value() == "A1+D1",
        "insert_columns reacquire post-noop edit should keep the translated formula");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "insert_columns reacquire post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "insert_columns reacquire post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "insert_columns reacquire post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "insert_columns reacquire post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "insert_columns reacquire post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "insert_columns reacquire post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "insert_columns reacquire post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "insert_columns reacquire post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "insert_columns reacquire post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "insert_columns reacquire post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output, "insert_columns reacquire post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "insert_columns reacquire post-noop save reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 6,
                "insert_columns reacquire post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 6) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    "insert_columns reacquire post-noop save reopened sparse_cells should keep source A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 4 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    "insert_columns reacquire post-noop save reopened sparse_cells should keep shifted D1 second");
                check(all_cells[2].reference.row == 2 &&
                        all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    "insert_columns reacquire post-noop save reopened sparse_cells should keep source A2 third");
                check(all_cells[3].reference.row == 2 &&
                        all_cells[3].reference.column == 5 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                        all_cells[3].value.text_value() == "A1+D1",
                    "insert_columns reacquire post-noop save reopened sparse_cells should keep translated E2 fourth");
                check(all_cells[4].reference.row == 3 &&
                        all_cells[4].reference.column == 5 &&
                        all_cells[4].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[4].value.text_value() == "extra-c3",
                    "insert_columns reacquire post-noop save reopened sparse_cells should keep shifted E3 fifth");
                check(all_cells[5].reference.row == 3 &&
                        all_cells[5].reference.column == 6 &&
                        all_cells[5].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[5].value.text_value() == "post-noop-insert-columns",
                    "insert_columns reacquire post-noop save reopened sparse_cells should keep post-noop F3 sixth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A1:F3");
            check(shifted_range.size() == 6,
                "insert_columns reacquire post-noop save reopened range sparse_cells should expose all records");
            const std::array<fastxlsx::WorksheetCellReference, 9> requested_refs {
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {1, 4},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {2, 3},
                fastxlsx::WorksheetCellReference {2, 5},
                fastxlsx::WorksheetCellReference {3, 3},
                fastxlsx::WorksheetCellReference {3, 5},
                fastxlsx::WorksheetCellReference {3, 6},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 6,
                "insert_columns reacquire post-noop save reopened requested sparse_cells should skip inserted and old coordinates");
            if (requested_cells.size() == 6) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 1 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[0].value.text_value() == "placeholder-a1",
                    "insert_columns reacquire post-noop save reopened requested sparse_cells should keep A1 first");
                check(requested_cells[1].reference.row == 1 &&
                        requested_cells[1].reference.column == 4 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[1].value.number_value() == 1.0,
                    "insert_columns reacquire post-noop save reopened requested sparse_cells should keep D1 second");
                check(requested_cells[2].reference.row == 2 &&
                        requested_cells[2].reference.column == 1 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "placeholder-a2",
                    "insert_columns reacquire post-noop save reopened requested sparse_cells should keep A2 third");
                check(requested_cells[3].reference.row == 2 &&
                        requested_cells[3].reference.column == 5 &&
                        requested_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                        requested_cells[3].value.text_value() == "A1+D1",
                    "insert_columns reacquire post-noop save reopened requested sparse_cells should keep E2 fourth");
                check(requested_cells[4].reference.row == 3 &&
                        requested_cells[4].reference.column == 5 &&
                        requested_cells[4].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[4].value.text_value() == "extra-c3",
                    "insert_columns reacquire post-noop save reopened requested sparse_cells should keep E3 fifth");
                check(requested_cells[5].reference.row == 3 &&
                        requested_cells[5].reference.column == 6 &&
                        requested_cells[5].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[5].value.text_value() == "post-noop-insert-columns",
                    "insert_columns reacquire post-noop save reopened requested sparse_cells should keep F3 sixth");
            }
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 6,
                "insert_columns reacquire post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a1",
                "insert_columns reacquire post-noop save reopened output should keep source A1");
            const fastxlsx::CellValue reopened_d1 = reopened_sheet.get_cell("D1");
            check(reopened_d1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_d1.number_value() == 1.0,
                "insert_columns reacquire post-noop save reopened output should keep shifted source B1");
            const fastxlsx::CellValue reopened_e2 = reopened_sheet.get_cell("E2");
            check(reopened_e2.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_e2.text_value() == "A1+D1",
                "insert_columns reacquire post-noop save reopened output should keep translated formula");
            const fastxlsx::CellValue reopened_e3 = reopened_sheet.get_cell("E3");
            check(reopened_e3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e3.text_value() == "extra-c3",
                "insert_columns reacquire post-noop save reopened output should keep shifted dirty cell");
            const fastxlsx::CellValue reopened_f3 = reopened_sheet.get_cell("F3");
            check(reopened_f3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_f3.text_value() == "post-noop-insert-columns",
                "insert_columns reacquire post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("C2").has_value() &&
                    !reopened_sheet.try_cell("C3").has_value(),
                "insert_columns reacquire post-noop save reopened output should keep inserted and old coordinates absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                reopened_sheet.row_cells(1);
            check(reopened_row_one.size() == 2 &&
                    reopened_row_one[0].reference.row == 1 &&
                    reopened_row_one[0].reference.column == 1 &&
                    reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[0].value.text_value() == "placeholder-a1" &&
                    reopened_row_one[1].reference.row == 1 &&
                    reopened_row_one[1].reference.column == 4 &&
                    reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_row_one[1].value.number_value() == 1.0,
                "insert_columns reacquire post-noop save reopened row_cells should expose source A1 and shifted D1");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 2 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 5 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_two[1].value.text_value() == "A1+D1",
                "insert_columns reacquire post-noop save reopened row_cells should expose source A2 and formula E2");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                reopened_sheet.row_cells(3);
            check(reopened_row_three.size() == 2 &&
                    reopened_row_three[0].reference.row == 3 &&
                    reopened_row_three[0].reference.column == 5 &&
                    reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[0].value.text_value() == "extra-c3" &&
                    reopened_row_three[1].reference.row == 3 &&
                    reopened_row_three[1].reference.column == 6 &&
                    reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[1].value.text_value() == "post-noop-insert-columns",
                "insert_columns reacquire post-noop save reopened row_cells should expose shifted E3 and post-noop F3");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_five =
                reopened_sheet.column_cells(5);
            check(reopened_column_five.size() == 2 &&
                    reopened_column_five[0].reference.row == 2 &&
                    reopened_column_five[0].reference.column == 5 &&
                    reopened_column_five[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_five[0].value.text_value() == "A1+D1" &&
                    reopened_column_five[1].reference.row == 3 &&
                    reopened_column_five[1].reference.column == 5 &&
                    reopened_column_five[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_five[1].value.text_value() == "extra-c3",
                "insert_columns reacquire post-noop save reopened column_cells should expose formula E2 and shifted E3");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_six =
                reopened_sheet.column_cells(6);
            check(reopened_column_six.size() == 1 &&
                    reopened_column_six[0].reference.row == 3 &&
                    reopened_column_six[0].reference.column == 6 &&
                    reopened_column_six[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_six[0].value.text_value() == "post-noop-insert-columns",
                "insert_columns reacquire post-noop save reopened column_cells should expose post-noop F3");
        });
}

void test_public_worksheet_editor_shift_try_reacquire_reuses_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-try-reacquire-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-try-reacquire-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-try-reacquire-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-try-reacquire-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift try-reacquire pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift try-reacquire first save should clean the original borrowed handle");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift try-reacquire first save should clear dirty materialized diagnostics");
    check(editor.pending_change_count() == 1,
        "shift try-reacquire first save should record the materialized handoff");
    check(!editor.last_edit_error().has_value(),
        "shift try-reacquire first save should keep diagnostics clear");

    std::optional<fastxlsx::WorksheetEditor> maybe_reacquired =
        editor.try_worksheet("Data");
    check(maybe_reacquired.has_value(),
        "shift try-reacquire should find the saved shifted worksheet");
    if (!maybe_reacquired.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor reacquired = std::move(*maybe_reacquired);
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift try-reacquire should return the saved clean materialized session");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "shift try-reacquire should preserve sparse count on both handles");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift try-reacquire should reuse saved shifted state instead of reloading source");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift try-reacquire should keep old shifted coordinates absent on both handles");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "shift try-reacquire later shift should dirty the shared session on both handles");
    const std::vector<std::string> dirty_names = editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 1 && dirty_names[0] == "Data",
        "shift try-reacquire later shift should report only Data as dirty materialized");
    check(editor.pending_materialized_cell_count() == 3,
        "shift try-reacquire later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "shift try-reacquire later shift should report the shared memory estimate");
    const fastxlsx::CellValue shifted_number = reacquired.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "shift try-reacquire later shift should move the source-backed number");
    const fastxlsx::CellValue old_handle_number = sheet.get_cell("C1");
    check(old_handle_number.kind() == fastxlsx::CellValueKind::Number &&
            old_handle_number.number_value() == 1.0,
        "shift try-reacquire later shift should be visible through the older handle");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() && !reacquired.try_cell("A2").has_value(),
        "shift try-reacquire later shift should keep old sparse coordinates absent on both handles");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift try-reacquire second save should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift try-reacquire second save should clear dirty materialized diagnostics again");
    check(editor.pending_change_count() == 2,
        "shift try-reacquire second save should record the second materialized handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift try-reacquire first output should keep the first shifted bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift try-reacquire first output should keep B1 before the later column shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift try-reacquire first output should contain the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift try-reacquire first output should not include the later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift try-reacquire first output should omit the old row coordinate");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift try-reacquire second output should project the combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift try-reacquire second output should contain the later column shift");
    check_contains(second_xml, R"(<c r="A3")",
        "shift try-reacquire second output should retain the earlier row shift");
    check_not_contains(second_xml, R"(r="B1")",
        "shift try-reacquire second output should omit the old column coordinate");
    check_not_contains(second_xml, R"(r="A2")",
        "shift try-reacquire second output should keep the old row coordinate absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift try-reacquire second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "shift try-reacquire second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift try-reacquire second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift try-reacquire second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift try-reacquire second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift try-reacquire second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift try-reacquire second no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "shift try-reacquire second no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift try-reacquire second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift try-reacquire second no-op save should leave the source package unchanged");

    check_reopened_shift_output(second_output, "shift try-reacquire second save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift try-reacquire second save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift try-reacquire second save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift try-reacquire second save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift try-reacquire second save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift try-reacquire second save reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_try_reacquire_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-try-reacquire-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-try-reacquire-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-try-reacquire-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-try-reacquire-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-try-reacquire-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift try-reacquire noop save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift try-reacquire noop save first save should clean the original borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift try-reacquire noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift try-reacquire noop save first save should clear dirty materialized diagnostics");

    std::optional<fastxlsx::WorksheetEditor> maybe_reacquired =
        editor.try_worksheet("Data");
    check(maybe_reacquired.has_value(),
        "shift try-reacquire noop save should find the saved shifted worksheet");
    if (!maybe_reacquired.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor reacquired = std::move(*maybe_reacquired);
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift try-reacquire noop save should return the saved clean materialized session");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "shift try-reacquire noop save should preserve sparse count on both handles");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift try-reacquire noop save should reuse saved shifted state instead of reloading source");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift try-reacquire noop save should keep old shifted coordinates absent on both handles");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift try-reacquire noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift try-reacquire noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift try-reacquire noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift try-reacquire noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift try-reacquire noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift try-reacquire noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift try-reacquire noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "shift try-reacquire noop output should match the first save");
    check_reopened_shift_output(noop_output, "shift try-reacquire noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift try-reacquire noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift try-reacquire noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift try-reacquire noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift try-reacquire noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift try-reacquire noop save reopened output should omit later and old coordinates");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift try-reacquire repeat noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift try-reacquire repeat noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift try-reacquire repeat noop save should keep dirty materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift try-reacquire repeat noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift try-reacquire repeat noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift try-reacquire repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift try-reacquire repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift try-reacquire repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift try-reacquire repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift try-reacquire repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift try-reacquire repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "shift try-reacquire repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift try-reacquire repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift try-reacquire repeat noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift try-reacquire repeat noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift try-reacquire repeat noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift try-reacquire repeat noop save reopened output should omit later and old coordinates");
        });

    reacquired.set_cell("C3", fastxlsx::CellValue::text("post-noop-try-reacquire"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "shift try-reacquire post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "shift try-reacquire post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 3,
        "shift try-reacquire post-noop edit should expand bounds to C3");
    const fastxlsx::CellValue post_noop_cell = reacquired.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-try-reacquire",
        "shift try-reacquire post-noop edit should expose the new dirty cell");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "shift try-reacquire post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift try-reacquire post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "shift try-reacquire post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "shift try-reacquire post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift try-reacquire post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift try-reacquire post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift try-reacquire post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift try-reacquire post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift try-reacquire post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift try-reacquire post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output, "shift try-reacquire post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift try-reacquire post-noop save reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 4,
                "shift try-reacquire post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 4) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    "shift try-reacquire post-noop save reopened sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    "shift try-reacquire post-noop save reopened sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 &&
                        all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    "shift try-reacquire post-noop save reopened sparse_cells should keep shifted A3 third");
                check(all_cells[3].reference.row == 3 &&
                        all_cells[3].reference.column == 3 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[3].value.text_value() == "post-noop-try-reacquire",
                    "shift try-reacquire post-noop save reopened sparse_cells should keep post-noop C3 fourth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A2:C3");
            check(shifted_range.size() == 2 &&
                    shifted_range[0].reference.row == 3 &&
                    shifted_range[0].reference.column == 1 &&
                    shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[0].value.text_value() == "placeholder-a2" &&
                    shifted_range[1].reference.row == 3 &&
                    shifted_range[1].reference.column == 3 &&
                    shifted_range[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[1].value.text_value() == "post-noop-try-reacquire",
                "shift try-reacquire post-noop save reopened range sparse_cells should expose shifted row and post-noop edit");
            const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 3,
                "shift try-reacquire post-noop save reopened requested sparse_cells should skip old A2");
            if (requested_cells.size() == 3) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 2 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[0].value.number_value() == 1.0,
                    "shift try-reacquire post-noop save reopened requested sparse_cells should keep B1 input order");
                check(requested_cells[1].reference.row == 3 &&
                        requested_cells[1].reference.column == 1 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == "placeholder-a2",
                    "shift try-reacquire post-noop save reopened requested sparse_cells should return shifted A3");
                check(requested_cells[2].reference.row == 3 &&
                        requested_cells[2].reference.column == 3 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "post-noop-try-reacquire",
                    "shift try-reacquire post-noop save reopened requested sparse_cells should return post-noop C3");
            }
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift try-reacquire post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift try-reacquire post-noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift try-reacquire post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-try-reacquire",
                "shift try-reacquire post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift try-reacquire post-noop save reopened output should omit later and old coordinates");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                reopened_sheet.row_cells(3);
            check(reopened_row_three.size() == 2 &&
                    reopened_row_three[0].reference.row == 3 &&
                    reopened_row_three[0].reference.column == 1 &&
                    reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_three[1].reference.row == 3 &&
                    reopened_row_three[1].reference.column == 3 &&
                    reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[1].value.text_value() == "post-noop-try-reacquire",
                "shift try-reacquire post-noop save reopened row_cells should expose shifted row and post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 3 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_three[0].value.text_value() == "post-noop-try-reacquire",
                "shift try-reacquire post-noop save reopened column_cells should expose post-noop edit");
        });
}

void test_public_worksheet_editor_shift_reacquire_option_mismatch_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-options-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-options-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-options-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-options-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire option mismatch pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire option mismatch first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire option mismatch first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire option mismatch first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire option mismatch first save should keep diagnostics clear");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_option_mismatch =
        workbook_editor_public_catalog_snapshot(editor);

    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("Data", mismatched_options);
    }), "shift reacquire option mismatch try_worksheet should reject different options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data", mismatched_options);
    }), "shift reacquire option mismatch worksheet should reject different options");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire option mismatch should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "shift reacquire option mismatch should leave the saved handle clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire option mismatch should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire option mismatch should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire option mismatch should not queue replacement diagnostics");
    check_workbook_editor_public_catalog_preserved(editor, catalog_before_option_mismatch,
        "shift reacquire option mismatch");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire option mismatch should preserve the saved shifted row");
    check(!sheet.try_cell("A2").has_value(),
        "shift reacquire option mismatch should keep old shifted rows absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire option mismatch matching reacquire should stay clean");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire option mismatch matching reacquire should reuse saved shifted state");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire option mismatch matching reacquire should keep old row absent");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "shift reacquire option mismatch later shift should dirty the shared session");
    const std::vector<std::string> dirty_names = editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 1 && dirty_names[0] == "Data",
        "shift reacquire option mismatch later shift should report Data dirty once");
    check(editor.pending_materialized_cell_count() == 3,
        "shift reacquire option mismatch later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "shift reacquire option mismatch later shift should report the shared memory estimate");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "shift reacquire option mismatch later shift should be visible through the older handle");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            !reacquired.try_cell("B1").has_value(),
        "shift reacquire option mismatch later shift should retain the row shift and move columns");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire option mismatch second save should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire option mismatch second save should clear dirty diagnostics again");
    check(editor.pending_change_count() == 2,
        "shift reacquire option mismatch second save should record the later handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire option mismatch first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire option mismatch first output should keep B1 before later shift");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire option mismatch first output should not include later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire option mismatch second output should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire option mismatch second output should include shifted B1");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire option mismatch second output should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire option mismatch second output should keep old row coordinate absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire option mismatch second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire option mismatch second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire option mismatch second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire option mismatch second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire option mismatch second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire option mismatch second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire option mismatch second no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "shift reacquire option mismatch second no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire option mismatch second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire option mismatch second no-op save should leave the source package unchanged");

    check_reopened_shift_output(second_output, "shift reacquire option mismatch second save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire option mismatch reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire option mismatch reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire option mismatch reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire option mismatch reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire option mismatch reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_option_mismatch_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-options-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-options-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-options-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-options-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-options-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire option mismatch noop save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire option mismatch noop save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire option mismatch noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire option mismatch noop save first save should clear dirty diagnostics");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_option_mismatch =
        workbook_editor_public_catalog_snapshot(editor);

    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("Data", mismatched_options);
    }), "shift reacquire option mismatch noop save try_worksheet should reject different options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data", mismatched_options);
    }), "shift reacquire option mismatch noop save worksheet should reject different options");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire option mismatch noop save should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "shift reacquire option mismatch noop save should leave the saved handle clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire option mismatch noop save should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire option mismatch noop save should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire option mismatch noop save should not queue replacement diagnostics");
    check_workbook_editor_public_catalog_preserved(editor, catalog_before_option_mismatch,
        "shift reacquire option mismatch noop save");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire option mismatch noop save should preserve the saved shifted row");
    check(!sheet.try_cell("A2").has_value(),
        "shift reacquire option mismatch noop save should keep old shifted rows absent");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire option mismatch noop save should keep the original handle clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire option mismatch noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire option mismatch noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire option mismatch noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire option mismatch noop save should keep diagnostics clear after save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire option mismatch noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "shift reacquire option mismatch noop output should match the first save");
    check_reopened_shift_output(noop_output, "shift reacquire option mismatch noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire option mismatch noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire option mismatch noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire option mismatch noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire option mismatch noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire option mismatch noop save reopened output should omit later and old coordinates");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire option mismatch repeat noop save should keep the original handle clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire option mismatch repeat noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire option mismatch repeat noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire option mismatch repeat noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire option mismatch repeat noop save should keep diagnostics clear after save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire option mismatch repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire option mismatch repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire option mismatch repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire option mismatch repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire option mismatch repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire option mismatch repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output,
        "shift reacquire option mismatch repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire option mismatch repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire option mismatch repeat noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire option mismatch repeat noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire option mismatch repeat noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire option mismatch repeat noop save reopened output should omit later and old coordinates");
        });

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire option mismatch post-noop matching reacquire should stay clean");
    reacquired.set_cell("C3", fastxlsx::CellValue::text("post-noop-option-mismatch"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "shift reacquire option mismatch post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "shift reacquire option mismatch post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 3,
        "shift reacquire option mismatch post-noop edit should expand bounds to C3");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-option-mismatch",
        "shift reacquire option mismatch post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "shift reacquire option mismatch post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire option mismatch post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "shift reacquire option mismatch post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire option mismatch post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire option mismatch post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire option mismatch post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire option mismatch post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire option mismatch post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire option mismatch post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire option mismatch post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output, "shift reacquire option mismatch post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire option mismatch post-noop save reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 4,
                "shift reacquire option mismatch post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 4) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    "shift reacquire option mismatch post-noop save reopened sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    "shift reacquire option mismatch post-noop save reopened sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 &&
                        all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    "shift reacquire option mismatch post-noop save reopened sparse_cells should keep shifted A3 third");
                check(all_cells[3].reference.row == 3 &&
                        all_cells[3].reference.column == 3 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[3].value.text_value() == "post-noop-option-mismatch",
                    "shift reacquire option mismatch post-noop save reopened sparse_cells should keep post-noop C3 fourth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A2:C3");
            check(shifted_range.size() == 2 &&
                    shifted_range[0].reference.row == 3 &&
                    shifted_range[0].reference.column == 1 &&
                    shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[0].value.text_value() == "placeholder-a2" &&
                    shifted_range[1].reference.row == 3 &&
                    shifted_range[1].reference.column == 3 &&
                    shifted_range[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[1].value.text_value() == "post-noop-option-mismatch",
                "shift reacquire option mismatch post-noop save reopened range sparse_cells should expose shifted row and post-noop edit");
            const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 3,
                "shift reacquire option mismatch post-noop save reopened requested sparse_cells should skip old A2");
            if (requested_cells.size() == 3) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 2 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[0].value.number_value() == 1.0,
                    "shift reacquire option mismatch post-noop save reopened requested sparse_cells should keep B1 input order");
                check(requested_cells[1].reference.row == 3 &&
                        requested_cells[1].reference.column == 1 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == "placeholder-a2",
                    "shift reacquire option mismatch post-noop save reopened requested sparse_cells should return shifted A3");
                check(requested_cells[2].reference.row == 3 &&
                        requested_cells[2].reference.column == 3 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "post-noop-option-mismatch",
                    "shift reacquire option mismatch post-noop save reopened requested sparse_cells should return post-noop C3");
            }
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire option mismatch post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire option mismatch post-noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire option mismatch post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-option-mismatch",
                "shift reacquire option mismatch post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire option mismatch post-noop save reopened output should omit later and old coordinates");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                reopened_sheet.row_cells(3);
            check(reopened_row_three.size() == 2 &&
                    reopened_row_three[0].reference.row == 3 &&
                    reopened_row_three[0].reference.column == 1 &&
                    reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_three[1].reference.row == 3 &&
                    reopened_row_three[1].reference.column == 3 &&
                    reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[1].value.text_value() == "post-noop-option-mismatch",
                "shift reacquire option mismatch post-noop save reopened row_cells should expose shifted row and post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 3 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_three[0].value.text_value() == "post-noop-option-mismatch",
                "shift reacquire option mismatch post-noop save reopened column_cells should expose post-noop edit");
        });
}

void test_public_worksheet_editor_shift_reacquire_missing_query_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire missing query pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire missing query first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire missing query first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire missing query first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing query first save should keep diagnostics clear");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_missing_query =
        workbook_editor_public_catalog_snapshot(editor);

    const std::optional<fastxlsx::WorksheetEditor> missing = editor.try_worksheet("Missing");
    check(!missing.has_value(),
        "shift reacquire missing query try_worksheet should report a missing sheet");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Missing"); }),
        "shift reacquire missing query worksheet should reject the missing sheet");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing query should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "shift reacquire missing query should leave the saved handle clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire missing query should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire missing query should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire missing query should not queue replacement diagnostics");
    check_workbook_editor_public_catalog_preserved(editor, catalog_before_missing_query,
        "shift reacquire missing query");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire missing query should preserve the saved shifted row");
    check(!sheet.try_cell("A2").has_value(),
        "shift reacquire missing query should keep old shifted rows absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire missing query matching reacquire should stay clean");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire missing query matching reacquire should reuse saved shifted state");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire missing query matching reacquire should keep old row absent");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "shift reacquire missing query later shift should dirty the shared session");
    const std::vector<std::string> dirty_names = editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 1 && dirty_names[0] == "Data",
        "shift reacquire missing query later shift should report Data dirty once");
    check(editor.pending_materialized_cell_count() == 3,
        "shift reacquire missing query later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "shift reacquire missing query later shift should report the shared memory estimate");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "shift reacquire missing query later shift should be visible through the older handle");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            !reacquired.try_cell("B1").has_value(),
        "shift reacquire missing query later shift should retain the row shift and move columns");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire missing query second save should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire missing query second save should clear dirty diagnostics again");
    check(editor.pending_change_count() == 2,
        "shift reacquire missing query second save should record the later handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire missing query first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire missing query first output should keep B1 before later shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire missing query first output should keep the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire missing query first output should not include later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire missing query first output should keep old row coordinate absent");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire missing query second output should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire missing query second output should include shifted B1");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire missing query second output should retain the shifted source row");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire missing query second output should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire missing query second output should keep old row coordinate absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire missing query second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire missing query second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire missing query second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire missing query second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing query second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire missing query second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire missing query second no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "shift reacquire missing query second no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire missing query second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire missing query second no-op save should leave the source package unchanged");

    check_reopened_shift_output(second_output, "shift reacquire missing query second save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire missing query reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire missing query reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire missing query reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire missing query reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire missing query reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_missing_query_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire missing query noop save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire missing query noop save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire missing query noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire missing query noop save first save should clear dirty diagnostics");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_missing_query =
        workbook_editor_public_catalog_snapshot(editor);

    const std::optional<fastxlsx::WorksheetEditor> missing = editor.try_worksheet("Missing");
    check(!missing.has_value(),
        "shift reacquire missing query noop save try_worksheet should report a missing sheet");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Missing"); }),
        "shift reacquire missing query noop save worksheet should reject the missing sheet");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing query noop save should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "shift reacquire missing query noop save should leave the saved handle clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire missing query noop save should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire missing query noop save should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire missing query noop save should not queue replacement diagnostics");
    check_workbook_editor_public_catalog_preserved(editor, catalog_before_missing_query,
        "shift reacquire missing query noop save");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire missing query noop save should preserve the saved shifted row");
    check(!sheet.try_cell("A2").has_value(),
        "shift reacquire missing query noop save should keep old shifted rows absent");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire missing query noop save should keep the original handle clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire missing query noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire missing query noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire missing query noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing query noop save should keep diagnostics clear after save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire missing query noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "shift reacquire missing query noop output should match the first save");
    check_reopened_shift_output(noop_output, "shift reacquire missing query noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire missing query noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire missing query noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire missing query noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire missing query noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire missing query noop save reopened output should omit later and old coordinates");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire missing query repeat noop save should keep the original handle clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire missing query repeat noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire missing query repeat noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire missing query repeat noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing query repeat noop save should keep diagnostics clear after save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire missing query repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire missing query repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire missing query repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire missing query repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire missing query repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire missing query repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output,
        "shift reacquire missing query repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire missing query repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire missing query repeat noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire missing query repeat noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire missing query repeat noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire missing query repeat noop save reopened output should omit later and old coordinates");
        });

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire missing query post-noop matching reacquire should stay clean");
    reacquired.set_cell("C3", fastxlsx::CellValue::text("post-noop-missing-query"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "shift reacquire missing query post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "shift reacquire missing query post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 3,
        "shift reacquire missing query post-noop edit should expand bounds to C3");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-missing-query",
        "shift reacquire missing query post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "shift reacquire missing query post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire missing query post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "shift reacquire missing query post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire missing query post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire missing query post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire missing query post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing query post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire missing query post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire missing query post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire missing query post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output, "shift reacquire missing query post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire missing query post-noop save reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 4,
                "shift reacquire missing query post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 4) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    "shift reacquire missing query post-noop save reopened sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    "shift reacquire missing query post-noop save reopened sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 &&
                        all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    "shift reacquire missing query post-noop save reopened sparse_cells should keep shifted A3 third");
                check(all_cells[3].reference.row == 3 &&
                        all_cells[3].reference.column == 3 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[3].value.text_value() == "post-noop-missing-query",
                    "shift reacquire missing query post-noop save reopened sparse_cells should keep post-noop C3 fourth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A2:C3");
            check(shifted_range.size() == 2 &&
                    shifted_range[0].reference.row == 3 &&
                    shifted_range[0].reference.column == 1 &&
                    shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[0].value.text_value() == "placeholder-a2" &&
                    shifted_range[1].reference.row == 3 &&
                    shifted_range[1].reference.column == 3 &&
                    shifted_range[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[1].value.text_value() == "post-noop-missing-query",
                "shift reacquire missing query post-noop save reopened range sparse_cells should expose shifted row and post-noop edit");
            const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 3,
                "shift reacquire missing query post-noop save reopened requested sparse_cells should skip old A2");
            if (requested_cells.size() == 3) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 2 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[0].value.number_value() == 1.0,
                    "shift reacquire missing query post-noop save reopened requested sparse_cells should keep B1 input order");
                check(requested_cells[1].reference.row == 3 &&
                        requested_cells[1].reference.column == 1 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == "placeholder-a2",
                    "shift reacquire missing query post-noop save reopened requested sparse_cells should return shifted A3");
                check(requested_cells[2].reference.row == 3 &&
                        requested_cells[2].reference.column == 3 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "post-noop-missing-query",
                    "shift reacquire missing query post-noop save reopened requested sparse_cells should return post-noop C3");
            }
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire missing query post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire missing query post-noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire missing query post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-missing-query",
                "shift reacquire missing query post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire missing query post-noop save reopened output should omit later and old coordinates");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                reopened_sheet.row_cells(3);
            check(reopened_row_three.size() == 2 &&
                    reopened_row_three[0].reference.row == 3 &&
                    reopened_row_three[0].reference.column == 1 &&
                    reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_three[1].reference.row == 3 &&
                    reopened_row_three[1].reference.column == 3 &&
                    reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[1].value.text_value() == "post-noop-missing-query",
                "shift reacquire missing query post-noop save reopened row_cells should expose shifted row and post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 3 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_three[0].value.text_value() == "post-noop-missing-query",
                "shift reacquire missing query post-noop save reopened column_cells should expose post-noop edit");
        });
}

void test_public_worksheet_editor_shift_reacquire_invalid_reads_preserve_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-read-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-read-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-read-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-read-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire invalid reads pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire invalid reads first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid reads first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire invalid reads first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid reads first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire invalid reads matching reacquire should stay clean before failures");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid reads matching reacquire should reuse saved shifted state");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_invalid_reads =
        workbook_editor_public_catalog_snapshot(editor);

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "shift reacquire invalid reads should reject row-zero try_cell on the original handle");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "shift reacquire invalid reads should reject column-zero get_cell on the reacquired handle");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "shift reacquire invalid reads should reject lowercase A1 reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("XFE1"); }),
        "shift reacquire invalid reads should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "shift reacquire invalid reads should reject invalid CellRange reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.sparse_cells("B2:A1"); }),
        "shift reacquire invalid reads should reject reversed A1 range reads");
    check(threw_fastxlsx_error([&] {
        const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1048577, 1},
        };
        (void)sheet.sparse_cells(invalid_batch);
    }), "shift reacquire invalid reads should reject invalid coordinate batch reads");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(0); }),
        "shift reacquire invalid reads should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)reacquired.column_cells(16385); }),
        "shift reacquire invalid reads should reject column_cells column overflow");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("D4"); }),
        "shift reacquire invalid reads should reject valid but missing get_cell reads");

    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid reads should not update last_edit_error");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid reads should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid reads should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire invalid reads should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid reads should not queue replacement diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid reads should keep worksheet edit summaries empty");
    check_workbook_editor_public_catalog_preserved(editor, catalog_before_invalid_reads,
        "shift reacquire invalid reads");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "shift reacquire invalid reads should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 2,
        "shift reacquire invalid reads should preserve shifted bounds");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid reads should preserve shifted source cells");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire invalid reads should keep old row coordinates absent");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "shift reacquire invalid reads later shift should dirty the shared session");
    const std::vector<std::string> dirty_names = editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 1 && dirty_names[0] == "Data",
        "shift reacquire invalid reads later shift should report Data dirty once");
    check(editor.pending_materialized_cell_count() == 3,
        "shift reacquire invalid reads later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "shift reacquire invalid reads later shift should report the shared memory estimate");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "shift reacquire invalid reads later shift should be visible through the older handle");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            !reacquired.try_cell("B1").has_value(),
        "shift reacquire invalid reads later shift should retain the row shift and move columns");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid reads second save should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire invalid reads second save should clear dirty diagnostics again");
    check(editor.pending_change_count() == 2,
        "shift reacquire invalid reads second save should record the later handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire invalid reads first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire invalid reads first output should keep B1 before later shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire invalid reads first output should keep the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire invalid reads first output should not include later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire invalid reads first output should keep old row coordinate absent");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire invalid reads second output should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire invalid reads second output should include shifted B1");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire invalid reads second output should retain the shifted source row");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire invalid reads second output should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire invalid reads second output should keep old row coordinate absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid reads second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire invalid reads second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire invalid reads second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid reads second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid reads second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire invalid reads second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire invalid reads second no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "shift reacquire invalid reads second no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire invalid reads second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire invalid reads second no-op save should leave the source package unchanged");

    check_reopened_shift_output(second_output, "shift reacquire invalid reads second save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire invalid reads reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire invalid reads reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire invalid reads reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid reads reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid reads reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_invalid_reads_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-read-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-read-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-read-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-read-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-read-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire invalid reads noop save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire invalid reads noop save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid reads noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid reads noop save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid reads noop save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire invalid reads noop save matching reacquire should stay clean before failures");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid reads noop save matching reacquire should reuse saved shifted state");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_invalid_reads =
        workbook_editor_public_catalog_snapshot(editor);

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "shift reacquire invalid reads noop save should reject row-zero try_cell on the original handle");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "shift reacquire invalid reads noop save should reject column-zero get_cell on the reacquired handle");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "shift reacquire invalid reads noop save should reject lowercase A1 reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("XFE1"); }),
        "shift reacquire invalid reads noop save should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "shift reacquire invalid reads noop save should reject invalid CellRange reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.sparse_cells("B2:A1"); }),
        "shift reacquire invalid reads noop save should reject reversed A1 range reads");
    check(threw_fastxlsx_error([&] {
        const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1048577, 1},
        };
        (void)sheet.sparse_cells(invalid_batch);
    }), "shift reacquire invalid reads noop save should reject invalid coordinate batch reads");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(0); }),
        "shift reacquire invalid reads noop save should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)reacquired.column_cells(16385); }),
        "shift reacquire invalid reads noop save should reject column_cells column overflow");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("D4"); }),
        "shift reacquire invalid reads noop save should reject valid but missing get_cell reads");

    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid reads noop save should not update last_edit_error");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid reads noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid reads noop save should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid reads noop save should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid reads noop save should not queue replacement diagnostics");
    check_workbook_editor_public_catalog_preserved(editor, catalog_before_invalid_reads,
        "shift reacquire invalid reads noop save");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "shift reacquire invalid reads noop save should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 2,
        "shift reacquire invalid reads noop save should preserve shifted bounds");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid reads noop save should preserve shifted source cells");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire invalid reads noop save should keep old row coordinates absent");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid reads noop save should keep both handles clean after no-op save");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid reads noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid reads noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid reads noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid reads noop save should keep diagnostics clear after save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire invalid reads noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "shift reacquire invalid reads noop output should match the first save");
    check_reopened_shift_output(noop_output, "shift reacquire invalid reads noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire invalid reads noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire invalid reads noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire invalid reads noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid reads noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid reads noop save reopened output should omit later and old coordinates");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid reads repeat noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid reads repeat noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid reads repeat noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid reads repeat noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid reads repeat noop save should keep diagnostics clear after save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire invalid reads repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire invalid reads repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire invalid reads repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire invalid reads repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire invalid reads repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire invalid reads repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output,
        "shift reacquire invalid reads repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire invalid reads repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire invalid reads repeat noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire invalid reads repeat noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid reads repeat noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid reads repeat noop save reopened output should omit later and old coordinates");
        });

    reacquired.set_cell("C3", fastxlsx::CellValue::text("post-noop-invalid-reads"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "shift reacquire invalid reads post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "shift reacquire invalid reads post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 3,
        "shift reacquire invalid reads post-noop edit should expand bounds to C3");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-invalid-reads",
        "shift reacquire invalid reads post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "shift reacquire invalid reads post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid reads post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "shift reacquire invalid reads post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire invalid reads post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid reads post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid reads post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid reads post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire invalid reads post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire invalid reads post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire invalid reads post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire invalid reads post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output, "shift reacquire invalid reads post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire invalid reads post-noop save reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 4,
                "shift reacquire invalid reads post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 4) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    "shift reacquire invalid reads post-noop save reopened sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    "shift reacquire invalid reads post-noop save reopened sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 &&
                        all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    "shift reacquire invalid reads post-noop save reopened sparse_cells should keep shifted A3 third");
                check(all_cells[3].reference.row == 3 &&
                        all_cells[3].reference.column == 3 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[3].value.text_value() == "post-noop-invalid-reads",
                    "shift reacquire invalid reads post-noop save reopened sparse_cells should keep post-noop C3 fourth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A2:C3");
            check(shifted_range.size() == 2 &&
                    shifted_range[0].reference.row == 3 &&
                    shifted_range[0].reference.column == 1 &&
                    shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[0].value.text_value() == "placeholder-a2" &&
                    shifted_range[1].reference.row == 3 &&
                    shifted_range[1].reference.column == 3 &&
                    shifted_range[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[1].value.text_value() == "post-noop-invalid-reads",
                "shift reacquire invalid reads post-noop save reopened range sparse_cells should expose shifted row and post-noop edit");
            const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 3,
                "shift reacquire invalid reads post-noop save reopened requested sparse_cells should skip old A2");
            if (requested_cells.size() == 3) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 2 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[0].value.number_value() == 1.0,
                    "shift reacquire invalid reads post-noop save reopened requested sparse_cells should keep B1 input order");
                check(requested_cells[1].reference.row == 3 &&
                        requested_cells[1].reference.column == 1 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == "placeholder-a2",
                    "shift reacquire invalid reads post-noop save reopened requested sparse_cells should return shifted A3");
                check(requested_cells[2].reference.row == 3 &&
                        requested_cells[2].reference.column == 3 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "post-noop-invalid-reads",
                    "shift reacquire invalid reads post-noop save reopened requested sparse_cells should return post-noop C3");
            }
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire invalid reads post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire invalid reads post-noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid reads post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-invalid-reads",
                "shift reacquire invalid reads post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid reads post-noop save reopened output should omit later and old coordinates");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                reopened_sheet.row_cells(3);
            check(reopened_row_three.size() == 2 &&
                    reopened_row_three[0].reference.row == 3 &&
                    reopened_row_three[0].reference.column == 1 &&
                    reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_three[1].reference.row == 3 &&
                    reopened_row_three[1].reference.column == 3 &&
                    reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[1].value.text_value() == "post-noop-invalid-reads",
                "shift reacquire invalid reads post-noop save reopened row_cells should expose shifted row and post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 3 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_three[0].value.text_value() == "post-noop-invalid-reads",
                "shift reacquire invalid reads post-noop save reopened column_cells should expose post-noop edit");
        });
}

void test_public_worksheet_editor_shift_reacquire_invalid_mutations_preserve_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-mutation-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-mutation-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-mutation-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-mutation-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire invalid mutations pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire invalid mutations first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid mutations first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire invalid mutations first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid mutations first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire invalid mutations matching reacquire should stay clean before failures");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid mutations matching reacquire should reuse saved shifted state");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_invalid_mutations =
        workbook_editor_public_catalog_snapshot(editor);

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-shift-row-zero"));
    }), "shift reacquire invalid mutations should reject row-zero set_cell on the original handle");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell(1, 0, fastxlsx::CellValue::text("invalid-shift-column-zero"));
    }), "shift reacquire invalid mutations should reject column-zero set_cell on the reacquired handle");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(1048577, 1,
            fastxlsx::CellValue::text("invalid-shift-row-overflow"));
    }), "shift reacquire invalid mutations should reject row-overflow set_cell");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell(1, 16385,
            fastxlsx::CellValue::text("invalid-shift-column-overflow"));
    }), "shift reacquire invalid mutations should reject column-overflow set_cell");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-shift-lowercase-a1"));
    }), "shift reacquire invalid mutations should reject lowercase A1 set_cell");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("XFE1",
            fastxlsx::CellValue::text("invalid-shift-a1-column-overflow"));
    }), "shift reacquire invalid mutations should reject A1 column-overflow set_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell(0, 1); }),
        "shift reacquire invalid mutations should reject row-zero erase_cell");
    check(threw_fastxlsx_error([&] { reacquired.erase_cell(1, 16385); }),
        "shift reacquire invalid mutations should reject column-overflow erase_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "shift reacquire invalid mutations should reject range erase_cell references");
    check(threw_fastxlsx_error([&] { reacquired.erase_cell("a1"); }),
        "shift reacquire invalid mutations should reject lowercase A1 erase_cell");

    check(editor.last_edit_error().has_value(),
        "shift reacquire invalid mutations should populate last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(), "WorksheetEditor cell reference is invalid",
            "shift reacquire invalid mutations should expose the latest invalid reference diagnostic");
    }
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid mutations should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid mutations should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire invalid mutations should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid mutations should not queue replacement diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid mutations should keep worksheet edit summaries empty");
    check_workbook_editor_public_catalog_preserved(editor, catalog_before_invalid_mutations,
        "shift reacquire invalid mutations");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "shift reacquire invalid mutations should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 2,
        "shift reacquire invalid mutations should preserve shifted bounds");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid mutations should preserve shifted source cells");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire invalid mutations should keep old row coordinates absent");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid mutations later valid shift should clear diagnostics");
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "shift reacquire invalid mutations later shift should dirty the shared session");
    const std::vector<std::string> dirty_names = editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 1 && dirty_names[0] == "Data",
        "shift reacquire invalid mutations later shift should report Data dirty once");
    check(editor.pending_materialized_cell_count() == 3,
        "shift reacquire invalid mutations later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "shift reacquire invalid mutations later shift should report the shared memory estimate");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "shift reacquire invalid mutations later shift should be visible through the older handle");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            !reacquired.try_cell("B1").has_value(),
        "shift reacquire invalid mutations later shift should retain the row shift and move columns");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid mutations second save should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire invalid mutations second save should clear dirty diagnostics again");
    check(editor.pending_change_count() == 2,
        "shift reacquire invalid mutations second save should record the later handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire invalid mutations first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire invalid mutations first output should keep B1 before later shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire invalid mutations first output should keep the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire invalid mutations first output should not include later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire invalid mutations first output should keep old row coordinate absent");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire invalid mutations second output should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire invalid mutations second output should include shifted B1");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire invalid mutations second output should retain the shifted source row");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire invalid mutations second output should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire invalid mutations second output should keep old row coordinate absent");
    check_not_contains(second_xml, "invalid-shift-",
        "shift reacquire invalid mutations second output should not leak rejected payloads");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid mutations second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire invalid mutations second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire invalid mutations second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid mutations second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid mutations second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire invalid mutations second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire invalid mutations second no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "shift reacquire invalid mutations second no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire invalid mutations second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire invalid mutations second no-op save should leave the source package unchanged");

    check_reopened_shift_output(second_output, "shift reacquire invalid mutations second save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire invalid mutations reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire invalid mutations reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire invalid mutations reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid mutations reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid mutations reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_invalid_mutations_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-mutation-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-mutation-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-mutation-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-mutation-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-mutation-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire invalid mutations noop save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire invalid mutations noop save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid mutations noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid mutations noop save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid mutations noop save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire invalid mutations noop save matching reacquire should stay clean before failures");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid mutations noop save matching reacquire should reuse saved shifted state");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_invalid_mutations =
        workbook_editor_public_catalog_snapshot(editor);

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-shift-noop-row-zero"));
    }), "shift reacquire invalid mutations noop save should reject row-zero set_cell on the original handle");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell(1, 0, fastxlsx::CellValue::text("invalid-shift-noop-column-zero"));
    }), "shift reacquire invalid mutations noop save should reject column-zero set_cell on the reacquired handle");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(1048577, 1,
            fastxlsx::CellValue::text("invalid-shift-noop-row-overflow"));
    }), "shift reacquire invalid mutations noop save should reject row-overflow set_cell");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell(1, 16385,
            fastxlsx::CellValue::text("invalid-shift-noop-column-overflow"));
    }), "shift reacquire invalid mutations noop save should reject column-overflow set_cell");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-shift-noop-lowercase-a1"));
    }), "shift reacquire invalid mutations noop save should reject lowercase A1 set_cell");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("XFE1",
            fastxlsx::CellValue::text("invalid-shift-noop-a1-column-overflow"));
    }), "shift reacquire invalid mutations noop save should reject A1 column-overflow set_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell(0, 1); }),
        "shift reacquire invalid mutations noop save should reject row-zero erase_cell");
    check(threw_fastxlsx_error([&] { reacquired.erase_cell(1, 16385); }),
        "shift reacquire invalid mutations noop save should reject column-overflow erase_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "shift reacquire invalid mutations noop save should reject range erase_cell references");
    check(threw_fastxlsx_error([&] { reacquired.erase_cell("a1"); }),
        "shift reacquire invalid mutations noop save should reject lowercase A1 erase_cell");

    const std::optional<std::string> invalid_error = editor.last_edit_error();
    check(invalid_error.has_value(),
        "shift reacquire invalid mutations noop save should populate last_edit_error");
    if (invalid_error.has_value()) {
        check_contains(*invalid_error, "WorksheetEditor cell reference is invalid",
            "shift reacquire invalid mutations noop save should expose the latest invalid reference diagnostic");
    }
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid mutations noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid mutations noop save should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid mutations noop save should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid mutations noop save should not queue replacement diagnostics");
    check_workbook_editor_public_catalog_preserved(editor, catalog_before_invalid_mutations,
        "shift reacquire invalid mutations noop save");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "shift reacquire invalid mutations noop save should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 2,
        "shift reacquire invalid mutations noop save should preserve shifted bounds");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid mutations noop save should preserve shifted source cells");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire invalid mutations noop save should keep old row coordinates absent");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid mutations noop save should keep both handles clean after no-op save");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid mutations noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid mutations noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid mutations noop save should keep replacement diagnostics clear");
    check(editor.last_edit_error() == invalid_error,
        "shift reacquire invalid mutations noop save should preserve invalid mutation diagnostic");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire invalid mutations noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "shift reacquire invalid mutations noop output should match the first save");
    const std::string noop_xml = noop_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(noop_xml, "invalid-shift-noop-",
        "shift reacquire invalid mutations noop output should not leak rejected payloads");
    check_reopened_shift_output(noop_output, "shift reacquire invalid mutations noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire invalid mutations noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire invalid mutations noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire invalid mutations noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid mutations noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid mutations noop save reopened output should omit later and old coordinates");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid mutations repeat noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid mutations repeat noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid mutations repeat noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid mutations repeat noop save should keep replacement diagnostics clear");
    check(editor.last_edit_error() == invalid_error,
        "shift reacquire invalid mutations repeat noop save should preserve invalid mutation diagnostic");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire invalid mutations repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire invalid mutations repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire invalid mutations repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire invalid mutations repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire invalid mutations repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire invalid mutations repeat noop output should match the first no-op output");
    const std::string second_noop_xml = second_noop_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(second_noop_xml, "invalid-shift-noop-",
        "shift reacquire invalid mutations repeat noop output should not leak rejected payloads");
    check_reopened_shift_output(second_noop_output, "shift reacquire invalid mutations repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire invalid mutations repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire invalid mutations repeat noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire invalid mutations repeat noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid mutations repeat noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid mutations repeat noop save reopened output should omit later and old coordinates");
        });

    reacquired.set_cell("C3", fastxlsx::CellValue::text("post-noop-invalid-mutations"));
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid mutations post-noop edit should clear the preserved diagnostic");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "shift reacquire invalid mutations post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "shift reacquire invalid mutations post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 3,
        "shift reacquire invalid mutations post-noop edit should expand bounds to C3");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-invalid-mutations",
        "shift reacquire invalid mutations post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "shift reacquire invalid mutations post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid mutations post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "shift reacquire invalid mutations post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire invalid mutations post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid mutations post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid mutations post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid mutations post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire invalid mutations post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire invalid mutations post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire invalid mutations post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire invalid mutations post-noop save should leave the repeat no-op output unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(post_noop_xml, "invalid-shift-noop-",
        "shift reacquire invalid mutations post-noop output should not leak rejected payloads");
    check_reopened_shift_output(post_noop_output, "shift reacquire invalid mutations post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire invalid mutations post-noop save reopened output should keep sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 4,
                "shift reacquire invalid mutations post-noop save reopened sparse_cells should expose all records");
            if (all_cells.size() == 4) {
                check(all_cells[0].reference.row == 1 &&
                        all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    "shift reacquire invalid mutations post-noop save reopened sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 &&
                        all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    "shift reacquire invalid mutations post-noop save reopened sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 &&
                        all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    "shift reacquire invalid mutations post-noop save reopened sparse_cells should keep shifted A3 third");
                check(all_cells[3].reference.row == 3 &&
                        all_cells[3].reference.column == 3 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[3].value.text_value() == "post-noop-invalid-mutations",
                    "shift reacquire invalid mutations post-noop save reopened sparse_cells should keep post-noop C3 fourth");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                reopened_sheet.sparse_cells("A2:C3");
            check(shifted_range.size() == 2 &&
                    shifted_range[0].reference.row == 3 &&
                    shifted_range[0].reference.column == 1 &&
                    shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[0].value.text_value() == "placeholder-a2" &&
                    shifted_range[1].reference.row == 3 &&
                    shifted_range[1].reference.column == 3 &&
                    shifted_range[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    shifted_range[1].value.text_value() == "post-noop-invalid-mutations",
                "shift reacquire invalid mutations post-noop save reopened range sparse_cells should expose shifted row and post-noop edit");
            const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 3,
                "shift reacquire invalid mutations post-noop save reopened requested sparse_cells should skip old A2");
            if (requested_cells.size() == 3) {
                check(requested_cells[0].reference.row == 1 &&
                        requested_cells[0].reference.column == 2 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[0].value.number_value() == 1.0,
                    "shift reacquire invalid mutations post-noop save reopened requested sparse_cells should keep B1 input order");
                check(requested_cells[1].reference.row == 3 &&
                        requested_cells[1].reference.column == 1 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == "placeholder-a2",
                    "shift reacquire invalid mutations post-noop save reopened requested sparse_cells should return shifted A3");
                check(requested_cells[2].reference.row == 3 &&
                        requested_cells[2].reference.column == 3 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "post-noop-invalid-mutations",
                    "shift reacquire invalid mutations post-noop save reopened requested sparse_cells should return post-noop C3");
            }
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire invalid mutations post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire invalid mutations post-noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid mutations post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-invalid-mutations",
                "shift reacquire invalid mutations post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid mutations post-noop save reopened output should omit later and old coordinates");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                reopened_sheet.row_cells(3);
            check(reopened_row_three.size() == 2 &&
                    reopened_row_three[0].reference.row == 3 &&
                    reopened_row_three[0].reference.column == 1 &&
                    reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_three[1].reference.row == 3 &&
                    reopened_row_three[1].reference.column == 3 &&
                    reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_three[1].value.text_value() == "post-noop-invalid-mutations",
                "shift reacquire invalid mutations post-noop save reopened row_cells should expose shifted row and post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 3 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_three[0].value.text_value() == "post-noop-invalid-mutations",
                "shift reacquire invalid mutations post-noop save reopened column_cells should expose post-noop edit");
        });
}

void test_public_worksheet_editor_shift_reacquire_invalid_shifts_noop_save_preserves_saved_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-shift-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-shift-noop-first-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-shift-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-shift-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-invalid-shift-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire invalid shifts noop save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire invalid shifts noop save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid shifts noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid shifts noop save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid shifts noop save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire invalid shifts noop save matching reacquire should stay clean before failures");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid shifts noop save matching reacquire should reuse saved shifted state");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_invalid_shifts =
        workbook_editor_public_catalog_snapshot(editor);

    check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
        "shift reacquire invalid shifts noop save should reject row-zero insert_rows");
    check(threw_fastxlsx_error([&] { reacquired.delete_rows(1048576, 2); }),
        "shift reacquire invalid shifts noop save should reject overflowing delete_rows");
    check(threw_fastxlsx_error([&] { sheet.insert_columns(0, 1); }),
        "shift reacquire invalid shifts noop save should reject column-zero insert_columns");
    check(threw_fastxlsx_error([&] { reacquired.delete_columns(16384, 2); }),
        "shift reacquire invalid shifts noop save should reject overflowing delete_columns");

    const std::optional<std::string> invalid_shift_error = editor.last_edit_error();
    check(invalid_shift_error.has_value(),
        "shift reacquire invalid shifts noop save should populate last_edit_error");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid shifts noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid shifts noop save should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid shifts noop save should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid shifts noop save should not queue replacement diagnostics");
    check_workbook_editor_public_catalog_preserved(editor, catalog_before_invalid_shifts,
        "shift reacquire invalid shifts noop save");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "shift reacquire invalid shifts noop save should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 2,
        "shift reacquire invalid shifts noop save should preserve shifted bounds");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire invalid shifts noop save should preserve shifted source cells");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire invalid shifts noop save should keep old row coordinates absent");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);

    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid shifts noop save should keep both handles clean after no-op save");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid shifts noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid shifts noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid shifts noop save should keep replacement diagnostics clear");
    check(editor.last_edit_error() == invalid_shift_error,
        "shift reacquire invalid shifts noop save should preserve invalid shift diagnostic");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire invalid shifts noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == first_entries,
        "shift reacquire invalid shifts noop output should match the first save");
    check_reopened_shift_output(noop_output, "shift reacquire invalid shifts noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire invalid shifts noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire invalid shifts noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire invalid shifts noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid shifts noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid shifts noop save reopened output should omit later and old coordinates");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid shifts repeat noop save should keep both handles clean");
    check(editor.pending_change_count() == 1,
        "shift reacquire invalid shifts repeat noop save should still not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid shifts repeat noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid shifts repeat noop save should keep replacement diagnostics clear");
    check(editor.last_edit_error() == invalid_shift_error,
        "shift reacquire invalid shifts repeat noop save should preserve invalid shift diagnostic");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire invalid shifts repeat noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire invalid shifts repeat noop save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire invalid shifts repeat noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire invalid shifts repeat noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire invalid shifts repeat noop save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire invalid shifts repeat noop output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "shift reacquire invalid shifts repeat noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire invalid shifts repeat noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                "shift reacquire invalid shifts repeat noop save reopened output should expose first-shift bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire invalid shifts repeat noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid shifts repeat noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid shifts repeat noop save reopened output should omit later and old coordinates");
        });

    reacquired.set_cell("C3", fastxlsx::CellValue::text("post-noop-invalid-shifts"));
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid shifts post-noop edit should clear the preserved diagnostic");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "shift reacquire invalid shifts post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "shift reacquire invalid shifts post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 3,
        "shift reacquire invalid shifts post-noop edit should expand bounds to C3");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-invalid-shifts",
        "shift reacquire invalid shifts post-noop edit should be visible through the older handle");
    const auto check_post_noop_invalid_shift_snapshots =
        [](fastxlsx::WorksheetEditor& snapshot_sheet, std::string_view scenario) {
            const std::string label(scenario);
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                snapshot_sheet.sparse_cells();
            check(all_cells.size() == 4,
                label + " sparse_cells should expose shifted and post-noop dirty cells");
            if (all_cells.size() == 4) {
                check(all_cells[0].reference.row == 1 && all_cells[0].reference.column == 1 &&
                        all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[0].value.text_value() == "placeholder-a1",
                    label + " sparse_cells should keep A1 first");
                check(all_cells[1].reference.row == 1 && all_cells[1].reference.column == 2 &&
                        all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        all_cells[1].value.number_value() == 1.0,
                    label + " sparse_cells should keep B1 second");
                check(all_cells[2].reference.row == 3 && all_cells[2].reference.column == 1 &&
                        all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[2].value.text_value() == "placeholder-a2",
                    label + " sparse_cells should keep shifted A2 as A3");
                check(all_cells[3].reference.row == 3 && all_cells[3].reference.column == 3 &&
                        all_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        all_cells[3].value.text_value() == "post-noop-invalid-shifts",
                    label + " sparse_cells should keep post-noop C3 last");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
                snapshot_sheet.sparse_cells("A2:C3");
            check(shifted_range.size() == 2,
                label + " range sparse_cells should expose only shifted row-three cells");
            if (shifted_range.size() == 2) {
                check(shifted_range[0].reference.row == 3 &&
                        shifted_range[0].reference.column == 1 &&
                        shifted_range[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        shifted_range[0].value.text_value() == "placeholder-a2",
                    label + " range sparse_cells should keep shifted A3 first");
                check(shifted_range[1].reference.row == 3 &&
                        shifted_range[1].reference.column == 3 &&
                        shifted_range[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        shifted_range[1].value.text_value() == "post-noop-invalid-shifts",
                    label + " range sparse_cells should keep post-noop C3 second");
            }

            const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
                fastxlsx::WorksheetCellReference {3, 3},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {3, 1},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                snapshot_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 3,
                label + " requested sparse_cells should skip the old shifted coordinate");
            if (requested_cells.size() == 3) {
                check(requested_cells[0].reference.row == 3 &&
                        requested_cells[0].reference.column == 3 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[0].value.text_value() == "post-noop-invalid-shifts",
                    label + " requested sparse_cells should keep post-noop C3 input order");
                check(requested_cells[1].reference.row == 1 &&
                        requested_cells[1].reference.column == 2 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        requested_cells[1].value.number_value() == 1.0,
                    label + " requested sparse_cells should keep B1 after skipped A2");
                check(requested_cells[2].reference.row == 3 &&
                        requested_cells[2].reference.column == 1 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[2].value.text_value() == "placeholder-a2",
                    label + " requested sparse_cells should keep shifted A3 last");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                snapshot_sheet.row_cells(3);
            check(row_three.size() == 2,
                label + " row_cells should expose shifted and post-noop cells");
            if (row_three.size() == 2) {
                check(row_three[0].reference.row == 3 &&
                        row_three[0].reference.column == 1 &&
                        row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_three[0].value.text_value() == "placeholder-a2",
                    label + " row_cells should keep shifted A3 first");
                check(row_three[1].reference.row == 3 &&
                        row_three[1].reference.column == 3 &&
                        row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_three[1].value.text_value() == "post-noop-invalid-shifts",
                    label + " row_cells should keep post-noop C3 second");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                snapshot_sheet.column_cells(3);
            check(column_three.size() == 1 &&
                    column_three[0].reference.row == 3 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[0].value.text_value() == "post-noop-invalid-shifts",
                label + " column_cells should expose the post-noop C3 cell");
        };
    check_post_noop_invalid_shift_snapshots(
        sheet, "shift reacquire invalid shifts post-noop older handle");
    check_post_noop_invalid_shift_snapshots(
        reacquired, "shift reacquire invalid shifts post-noop reacquired handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 1, "shift reacquire invalid shifts post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire invalid shifts post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "shift reacquire invalid shifts post-noop save should record the second materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire invalid shifts post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire invalid shifts post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire invalid shifts post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire invalid shifts post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire invalid shifts post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire invalid shifts post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire invalid shifts post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire invalid shifts post-noop save should leave the repeat no-op output unchanged");
    check_reopened_shift_output(post_noop_output, "shift reacquire invalid shifts post-noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire invalid shifts post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire invalid shifts post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                "shift reacquire invalid shifts post-noop save reopened output should keep B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire invalid shifts post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-invalid-shifts",
                "shift reacquire invalid shifts post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("C1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire invalid shifts post-noop save reopened output should omit later and old coordinates");
            check_post_noop_invalid_shift_snapshots(reopened_sheet,
                "shift reacquire invalid shifts post-noop save reopened output");
        });
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_shift_handle_reuse_after_save_as();
        test_public_worksheet_editor_shift_reacquire_reuses_saved_session();
        test_public_worksheet_editor_shift_reacquire_noop_save_preserves_saved_session();
        test_public_worksheet_editor_insert_rows_reacquire_formula_noop_save_preserves_saved_session();
        test_public_worksheet_editor_delete_columns_reacquire_noop_save_preserves_saved_session();
        test_public_worksheet_editor_delete_rows_reacquire_noop_save_preserves_saved_session();
        test_public_worksheet_editor_insert_columns_reacquire_noop_save_preserves_saved_session();
        test_public_worksheet_editor_shift_try_reacquire_reuses_saved_session();
        test_public_worksheet_editor_shift_try_reacquire_noop_save_preserves_saved_session();
        test_public_worksheet_editor_shift_reacquire_option_mismatch_preserves_saved_session();
        test_public_worksheet_editor_shift_reacquire_option_mismatch_noop_save_preserves_saved_session();
        test_public_worksheet_editor_shift_reacquire_missing_query_preserves_saved_session();
        test_public_worksheet_editor_shift_reacquire_missing_query_noop_save_preserves_saved_session();
        test_public_worksheet_editor_shift_reacquire_invalid_reads_preserve_saved_session();
        test_public_worksheet_editor_shift_reacquire_invalid_reads_noop_save_preserves_saved_session();
        test_public_worksheet_editor_shift_reacquire_invalid_mutations_preserve_saved_session();
        test_public_worksheet_editor_shift_reacquire_invalid_mutations_noop_save_preserves_saved_session();
        test_public_worksheet_editor_shift_reacquire_invalid_shifts_noop_save_preserves_saved_session();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor public-state reacquire saved check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor public-state reacquire saved tests passed\n");
    return 0;
}
