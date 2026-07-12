#include "test_workbook_editor_public_state_support.hpp"

namespace {

void test_public_worksheet_editor_shift_reacquire_failed_save_preserves_dirty_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-failed-save-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-failed-save-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-failed-save-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-failed-save-second-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-failed-save-second-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-failed-save-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire failed save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire failed save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire failed save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire failed save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire failed save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire failed save matching reacquire should stay clean before the later shift");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire failed save matching reacquire should reuse saved shifted state");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "shift reacquire failed save matching reacquire should keep old row coordinates absent");

    reacquired.insert_columns(2, 1);
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "shift reacquire failed save later shift should dirty the shared session");
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire failed save dirty state before rejected save",
        1,
        3,
        shifted_memory);
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "shift reacquire failed save later shift should be visible through the older handle");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            !reacquired.try_cell("B1").has_value(),
        "shift reacquire failed save later shift should retain the row shift and move columns");

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "shift reacquire failed save should reject saving over the source workbook");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire failed save rejected source-overwrite",
        1,
        3,
        shifted_memory);
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire failed save should preserve shifted numeric cells after rejection");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire failed save should preserve shifted source rows after rejection");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() && !reacquired.try_cell("A2").has_value(),
        "shift reacquire failed save should keep old sparse coordinates absent after rejection");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_xml, R"(<dimension ref="A1:B2"/>)",
        "shift reacquire failed save should leave the source workbook bounds unchanged");
    check_contains(source_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire failed save should leave the source workbook B1 unchanged");
    check_contains(source_xml, R"(<c r="A2")",
        "shift reacquire failed save should leave the source workbook A2 unchanged");
    check_not_contains(source_xml, R"(r="A3")",
        "shift reacquire failed save should not write the row shift into the source workbook");
    check_not_contains(source_xml, R"(r="C1")",
        "shift reacquire failed save should not write the column shift into the source workbook");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire failed save first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire failed save first output should keep B1 before rejected later shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire failed save first output should contain the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire failed save first output should not include the rejected later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire failed save first output should keep old row coordinate absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire failed save safe retry should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire failed save safe retry should clear dirty diagnostics");
    check(editor.pending_change_count() == 2,
        "shift reacquire failed save safe retry should record the later handoff");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire failed save safe retry should keep diagnostics clear");
    check_shift_reacquire_retry_snapshots(
        sheet, "shift reacquire failed save safe retry original handle");
    check_shift_reacquire_retry_snapshots(
        reacquired, "shift reacquire failed save safe retry reacquired handle");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire failed save safe retry should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire failed save safe retry should include shifted B1");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire failed save safe retry should retain the shifted source row");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire failed save safe retry should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire failed save safe retry should keep old row coordinate absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire failed save second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire failed save second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire failed save second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire failed save second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire failed save second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire failed save second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire failed save second no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "shift reacquire failed save second no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire failed save second no-op save should leave the second output unchanged");

    check_reopened_shift_output(second_output, "shift reacquire failed save safe retry",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire failed save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire failed save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire failed save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire failed save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire failed save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(
                reopened_sheet, "shift reacquire failed save reopened output");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire failed save repeat no-op save should keep both handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire failed save repeat no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire failed save repeat no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire failed save repeat no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire failed save repeat no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire failed save repeat no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire failed save repeat no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire failed save repeat no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire failed save repeat no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire failed save repeat no-op save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire failed save repeat no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire failed save repeat no-op output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "shift reacquire failed save repeat no-op save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire failed save repeat no-op save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire failed save repeat no-op save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire failed save repeat no-op save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire failed save repeat no-op save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire failed save repeat no-op save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(
                reopened_sheet, "shift reacquire failed save repeat no-op save reopened output");
        });

    reacquired.set_cell("C3", fastxlsx::CellValue::text("post-noop-failed-save"));
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "shift reacquire failed save post-noop edit should dirty both shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "shift reacquire failed save post-noop edit should add one sparse cell on both handles");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 3,
        "shift reacquire failed save post-noop edit should keep combined bounds");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-failed-save",
        "shift reacquire failed save post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, reacquired, 2, "shift reacquire failed save post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire failed save post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 3,
        "shift reacquire failed save post-noop save should record the third materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire failed save post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire failed save post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire failed save post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire failed save post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire failed save post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire failed save post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire failed save post-noop save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire failed save post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire failed save post-noop save should leave the repeat no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<c r="C3")",
        "shift reacquire failed save post-noop save should write the post-noop C3 cell");
    check_reopened_shift_output(post_noop_output, "shift reacquire failed save post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire failed save post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire failed save post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire failed save post-noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire failed save post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-failed-save",
                "shift reacquire failed save post-noop save reopened output should keep post-noop edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire failed save post-noop save reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_after_failed_save_retry_reuses_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-second-output.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-third-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-third-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-third-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire after retry pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire after retry first save should clean the original borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire after retry first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire after retry first save should clear dirty diagnostics");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire after retry dirty state before rejected save",
        1,
        3,
        shifted_memory);

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "shift reacquire after retry should reject saving over the source workbook");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire after retry rejected source-overwrite should leave the source package unchanged");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire after retry rejected source-overwrite",
        1,
        3,
        shifted_memory);

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire after retry safe retry should clean both borrowed handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire after retry safe retry should clear dirty diagnostics");
    check(editor.pending_change_count() == 2,
        "shift reacquire after retry safe retry should record the second handoff");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire after retry safe retry should keep diagnostics clear");

    std::optional<fastxlsx::WorksheetEditor> maybe_after_retry =
        editor.try_worksheet("Data");
    check(maybe_after_retry.has_value(),
        "shift reacquire after retry should find the saved shifted worksheet");
    if (!maybe_after_retry.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor after_retry = std::move(*maybe_after_retry);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire after retry should return a clean saved session");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire after retry clean reacquire should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire after retry clean reacquire should not queue replacement diagnostics");
    check(editor.pending_change_count() == 2,
        "shift reacquire after retry clean reacquire should not add handoffs");
    check(after_retry.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire after retry should preserve the combined shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire after retry should expose the combined shifted number on all handles");
    check(!after_retry.try_cell("B1").has_value() &&
            !after_retry.try_cell("A2").has_value(),
        "shift reacquire after retry should keep old sparse coordinates absent");

    after_retry.delete_rows(3, 1);
    const std::size_t deleted_memory = after_retry.estimated_memory_usage();
    check(after_retry.has_pending_changes() && sheet.has_pending_changes() &&
            reacquired.has_pending_changes(),
        "shift reacquire after retry later delete should dirty all shared handles");
    {
        const std::vector<std::string> dirty_names =
            editor.pending_materialized_worksheet_names();
        check(dirty_names.size() == 1 && dirty_names[0] == "Data",
            "shift reacquire after retry later delete should report Data dirty once");
    }
    check(editor.pending_materialized_cell_count() == 2,
        "shift reacquire after retry later delete should shrink the dirty sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == deleted_memory,
        "shift reacquire after retry later delete should report the dirty memory");
    check(!after_retry.try_cell("A3").has_value() &&
            !sheet.try_cell("A3").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "shift reacquire after retry later delete should remove the shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0,
        "shift reacquire after retry later delete should preserve the shifted number");

    editor.save_as(third_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire after retry third save should clean all shared handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire after retry third save should clear dirty diagnostics");
    check(editor.pending_change_count() == 3,
        "shift reacquire after retry third save should record the third handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire after retry second output should keep combined shifted bounds");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire after retry second output should keep the shifted source row");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire after retry second output should keep the shifted number");

    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    const std::string third_xml = third_entries.at("xl/worksheets/sheet1.xml");
    check_contains(third_xml, R"(<dimension ref="A1:C1"/>)",
        "shift reacquire after retry third output should shrink after deleting row 3");
    check_contains(third_xml, R"(<c r="A1")",
        "shift reacquire after retry third output should keep A1");
    check_contains(third_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire after retry third output should keep shifted B1");
    check_not_contains(third_xml, R"(r="A3")",
        "shift reacquire after retry third output should omit deleted row 3");
    check_not_contains(third_xml, R"(r="B1")",
        "shift reacquire after retry third output should keep old B1 absent");
    check_not_contains(third_xml, R"(r="A2")",
        "shift reacquire after retry third output should keep old A2 absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire after retry third no-op save should keep all shared handles clean");
    check(editor.pending_change_count() == 3,
        "shift reacquire after retry third no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire after retry third no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire after retry third no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire after retry third no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire after retry third no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire after retry third no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == third_entries,
        "shift reacquire after retry third no-op output should match the third output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire after retry third no-op save should leave the source package unchanged");

    check_reopened_shift_output(third_output, "shift reacquire after retry third save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 2,
                "shift reacquire after retry reopened output should shrink sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 3,
                "shift reacquire after retry reopened output should expose shrunken bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire after retry reopened output should read shifted B1");
            check(!reopened_sheet.try_cell("A3").has_value() &&
                    !reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire after retry reopened output should keep deleted and old coordinates absent");
        });

    after_retry.set_cell("D1", fastxlsx::CellValue::text("post-noop-after-retry-delete"));
    check(after_retry.has_pending_changes() && sheet.has_pending_changes() &&
            reacquired.has_pending_changes(),
        "shift reacquire after retry third post-noop edit should dirty all shared handles");
    check(after_retry.cell_count() == 3 &&
            sheet.cell_count() == 3 &&
            reacquired.cell_count() == 3,
        "shift reacquire after retry third post-noop edit should add one sparse cell on all handles");
    check_cell_range_equals(after_retry.used_range(), 1, 1, 1, 4,
        "shift reacquire after retry third post-noop edit should expand bounds to D1");
    check(after_retry.get_cell("D1").text_value() == "post-noop-after-retry-delete" &&
            sheet.get_cell("D1").text_value() == "post-noop-after-retry-delete" &&
            reacquired.get_cell("D1").text_value() == "post-noop-after-retry-delete",
        "shift reacquire after retry third post-noop edit should be visible through all handles");
    check(!after_retry.try_cell("A3").has_value() &&
            !sheet.try_cell("A3").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "shift reacquire after retry third post-noop edit should keep the deleted row absent");
    check(after_retry.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire after retry third post-noop edit should keep shifted B1");
    check_public_state_single_data_dirty_materialized_summary(
        editor, after_retry, 3, "shift reacquire after retry third post-noop edit");

    editor.save_as(post_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire after retry third post-noop save should clean all shared handles");
    check(editor.pending_change_count() == 4,
        "shift reacquire after retry third post-noop save should record the fourth materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire after retry third post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire after retry third post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire after retry third post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire after retry third post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire after retry third post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire after retry third post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire after retry third post-noop save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(third_output) == third_entries,
        "shift reacquire after retry third post-noop save should leave the delete output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire after retry third post-noop save should leave the no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<dimension ref="A1:D1"/>)",
        "shift reacquire after retry third post-noop output should expand bounds to D1");
    check_contains(post_noop_xml, R"(<c r="D1")",
        "shift reacquire after retry third post-noop output should write the later D1 cell");
    check_contains(post_noop_xml, "post-noop-after-retry-delete",
        "shift reacquire after retry third post-noop output should write the later D1 text");
    check_not_contains(post_noop_xml, R"(r="A3")",
        "shift reacquire after retry third post-noop output should keep deleted A3 absent");
    check_not_contains(post_noop_xml, R"(r="B1")",
        "shift reacquire after retry third post-noop output should keep old B1 absent");
    check_not_contains(post_noop_xml, R"(r="A2")",
        "shift reacquire after retry third post-noop output should keep old A2 absent");
    check_reopened_shift_output(post_noop_output, "shift reacquire after retry third post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire after retry third post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 4,
                "shift reacquire after retry third post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire after retry third post-noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_d1 = reopened_sheet.get_cell("D1");
            check(reopened_d1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d1.text_value() == "post-noop-after-retry-delete",
                "shift reacquire after retry third post-noop save reopened output should keep post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 3 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "placeholder-a1" &&
                    row_one[1].reference.row == 1 &&
                    row_one[1].reference.column == 3 &&
                    row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    row_one[1].value.number_value() == 1.0 &&
                    row_one[2].reference.row == 1 &&
                    row_one[2].reference.column == 4 &&
                    row_one[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[2].value.text_value() == "post-noop-after-retry-delete",
                "shift reacquire after retry third post-noop row_cells should expose row-one order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                reopened_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 1 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "post-noop-after-retry-delete",
                "shift reacquire after retry third post-noop column_cells should expose the later edit");
            check(!reopened_sheet.try_cell("A3").has_value() &&
                    !reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire after retry third post-noop save reopened output should keep deleted and old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_after_failed_save_retry_noop_save_preserves_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-noop-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-noop-first-output.xlsx");
    const std::filesystem::path retry_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-noop-retry-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-after-retry-noop-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire after retry noop save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire after retry noop save first save should clean the original handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire after retry noop save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire after retry noop save first save should clear dirty diagnostics");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire after retry noop save dirty state before rejected save",
        1,
        3,
        shifted_memory);

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "shift reacquire after retry noop save should reject saving over the source workbook");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire after retry noop save rejected source-overwrite should leave the source package unchanged");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire after retry noop save rejected source-overwrite",
        1,
        3,
        shifted_memory);

    editor.save_as(retry_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire after retry noop save safe retry should clean existing handles");
    check(editor.pending_change_count() == 2,
        "shift reacquire after retry noop save safe retry should record the second handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire after retry noop save safe retry should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire after retry noop save safe retry should keep diagnostics clear");

    fastxlsx::WorksheetEditor after_retry = editor.worksheet("Data");
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire after retry noop save matching reacquire should return a clean session");
    check(editor.pending_change_count() == 2,
        "shift reacquire after retry noop save matching reacquire should not add handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire after retry noop save matching reacquire should keep diagnostics clear");
    check(after_retry.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire after retry noop save matching reacquire should preserve shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire after retry noop save matching reacquire should expose shifted number");
    check(!after_retry.try_cell("B1").has_value() &&
            !after_retry.try_cell("A2").has_value(),
        "shift reacquire after retry noop save matching reacquire should keep old coordinates absent");
    check_shift_reacquire_retry_snapshots(
        sheet, "shift reacquire after retry noop save original handle");
    check_shift_reacquire_retry_snapshots(
        reacquired, "shift reacquire after retry noop save reacquired handle");
    check_shift_reacquire_retry_snapshots(
        after_retry, "shift reacquire after retry noop save clean reacquire");

    const auto retry_entries = fastxlsx::test::read_zip_entries(retry_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire after retry noop save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire after retry noop save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire after retry noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire after retry noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire after retry noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire after retry noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire after retry noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == retry_entries,
        "shift reacquire after retry noop output should match the safe retry output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire after retry noop save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "shift reacquire after retry noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire after retry noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire after retry noop save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire after retry noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire after retry noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire after retry noop save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire after retry noop save reopened output");
        });

    after_retry.set_cell("D3", fastxlsx::CellValue::text("post-noop-after-retry"));
    check(after_retry.has_pending_changes() && sheet.has_pending_changes() &&
            reacquired.has_pending_changes(),
        "shift reacquire after retry noop post-noop edit should dirty all shared handles");
    check(sheet.cell_count() == 4 &&
            reacquired.cell_count() == 4 &&
            after_retry.cell_count() == 4,
        "shift reacquire after retry noop post-noop edit should add one sparse cell on all handles");
    check_cell_range_equals(after_retry.used_range(), 1, 1, 3, 4,
        "shift reacquire after retry noop post-noop edit should expand bounds to D3");
    check(sheet.get_cell("D3").text_value() == "post-noop-after-retry" &&
            reacquired.get_cell("D3").text_value() == "post-noop-after-retry" &&
            after_retry.get_cell("D3").text_value() == "post-noop-after-retry",
        "shift reacquire after retry noop post-noop edit should be visible through all handles");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("C1").number_value() == 1.0 &&
            after_retry.get_cell("C1").number_value() == 1.0,
        "shift reacquire after retry noop post-noop edit should preserve shifted source cells");
    check_public_state_single_data_dirty_materialized_summary(
        editor, after_retry, 2, "shift reacquire after retry noop post-noop edit");

    editor.save_as(post_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire after retry noop post-noop save should clean all shared handles");
    check(editor.pending_change_count() == 3,
        "shift reacquire after retry noop post-noop save should record the third handoff");
    check(editor.has_pending_changes(),
        "shift reacquire after retry noop post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire after retry noop post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire after retry noop post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire after retry noop post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire after retry noop post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(retry_output) == retry_entries,
        "shift reacquire after retry noop post-noop save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire after retry noop post-noop save should leave the no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<dimension ref="A1:D3"/>)",
        "shift reacquire after retry noop post-noop output should expand bounds to D3");
    check_contains(post_noop_xml, R"(<c r="D3")",
        "shift reacquire after retry noop post-noop output should write the later D3 cell");
    check_contains(post_noop_xml, "post-noop-after-retry",
        "shift reacquire after retry noop post-noop output should write the later D3 text");
    check_not_contains(post_noop_xml, R"(r="B1")",
        "shift reacquire after retry noop post-noop output should keep old B1 absent");
    check_not_contains(post_noop_xml, R"(r="A2")",
        "shift reacquire after retry noop post-noop output should keep old A2 absent");
    check_reopened_shift_output(post_noop_output, "shift reacquire after retry noop post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire after retry noop post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 4,
                "shift reacquire after retry noop post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire after retry noop post-noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire after retry noop post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_d3 = reopened_sheet.get_cell("D3");
            check(reopened_d3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d3.text_value() == "post-noop-after-retry",
                "shift reacquire after retry noop post-noop save reopened output should keep post-noop edit");
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
                    row_three[1].value.text_value() == "post-noop-after-retry",
                "shift reacquire after retry noop post-noop row_cells should expose shifted row order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                reopened_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 3 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "post-noop-after-retry",
                "shift reacquire after retry noop post-noop column_cells should expose the later edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire after retry noop post-noop save reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_path_equivalent_failed_save_preserves_dirty_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-path-equivalent-source.xlsx");
    const std::filesystem::path equivalent_source =
        source.parent_path() / "." / source.filename();
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-path-equivalent-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-path-equivalent-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-path-equivalent-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-path-equivalent-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-path-equivalent-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire path-equivalent failed save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire path-equivalent failed save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire path-equivalent failed save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire path-equivalent failed save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire path-equivalent failed save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire path-equivalent failed save matching reacquire should stay clean before the later shift");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire path-equivalent failed save matching reacquire should reuse saved shifted state");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire path-equivalent failed save dirty state before rejected save",
        1,
        3,
        shifted_memory);

    check(threw_fastxlsx_error([&] { editor.save_as(equivalent_source); }),
        "shift reacquire path-equivalent failed save should reject path-equivalent source overwrite");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire path-equivalent failed save rejected source-overwrite",
        1,
        3,
        shifted_memory);
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire path-equivalent failed save should preserve shifted numeric cells after rejection");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire path-equivalent failed save should preserve shifted source rows after rejection");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() && !reacquired.try_cell("A2").has_value(),
        "shift reacquire path-equivalent failed save should keep old sparse coordinates absent after rejection");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_xml, R"(<dimension ref="A1:B2"/>)",
        "shift reacquire path-equivalent failed save should leave the source workbook bounds unchanged");
    check_contains(source_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire path-equivalent failed save should leave the source workbook B1 unchanged");
    check_contains(source_xml, R"(<c r="A2")",
        "shift reacquire path-equivalent failed save should leave the source workbook A2 unchanged");
    check_not_contains(source_xml, R"(r="A3")",
        "shift reacquire path-equivalent failed save should not write the row shift into the source workbook");
    check_not_contains(source_xml, R"(r="C1")",
        "shift reacquire path-equivalent failed save should not write the column shift into the source workbook");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire path-equivalent failed save first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire path-equivalent failed save first output should keep B1 before rejected later shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire path-equivalent failed save first output should contain the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire path-equivalent failed save first output should not include the rejected later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire path-equivalent failed save first output should keep old row coordinate absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire path-equivalent failed save safe retry should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire path-equivalent failed save safe retry should clear dirty diagnostics");
    check(editor.pending_change_count() == 2,
        "shift reacquire path-equivalent failed save safe retry should record the later handoff");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire path-equivalent failed save safe retry should keep diagnostics clear");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire path-equivalent failed save safe retry should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire path-equivalent failed save safe retry should include shifted B1");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire path-equivalent failed save safe retry should retain the shifted source row");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire path-equivalent failed save safe retry should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire path-equivalent failed save safe retry should keep old row coordinate absent");

    check_reopened_shift_output(second_output, "shift reacquire path-equivalent failed save safe retry",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire path-equivalent failed save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire path-equivalent failed save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire path-equivalent failed save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire path-equivalent failed save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire path-equivalent failed save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire path-equivalent failed save reopened output");
        });

    fastxlsx::WorksheetEditor after_retry = editor.worksheet("Data");
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire path-equivalent failed save matching reacquire after retry should stay clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire path-equivalent failed save matching reacquire after retry should not add handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire path-equivalent failed save matching reacquire after retry should keep diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire path-equivalent failed save matching reacquire after retry should keep diagnostics clear");
    check(after_retry.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire path-equivalent failed save matching reacquire after retry should preserve shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire path-equivalent failed save matching reacquire after retry should expose shifted number");
    check(!after_retry.try_cell("B1").has_value() &&
            !after_retry.try_cell("A2").has_value(),
        "shift reacquire path-equivalent failed save matching reacquire after retry should keep old coordinates absent");
    check_shift_reacquire_retry_snapshots(
        sheet,
        "shift reacquire path-equivalent failed save matching reacquire after retry original handle");
    check_shift_reacquire_retry_snapshots(
        reacquired,
        "shift reacquire path-equivalent failed save matching reacquire after retry reacquired handle");
    check_shift_reacquire_retry_snapshots(
        after_retry,
        "shift reacquire path-equivalent failed save matching reacquire after retry clean handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire path-equivalent failed save noop save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire path-equivalent failed save noop save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire path-equivalent failed save noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire path-equivalent failed save noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire path-equivalent failed save noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire path-equivalent failed save noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire path-equivalent failed save noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "shift reacquire path-equivalent failed save noop output should match the safe retry output");
    check_reopened_shift_output(noop_output, "shift reacquire path-equivalent failed save noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire path-equivalent failed save noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire path-equivalent failed save noop save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire path-equivalent failed save noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire path-equivalent failed save noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire path-equivalent failed save noop save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire path-equivalent failed save noop save reopened output");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire path-equivalent failed save repeat no-op save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire path-equivalent failed save repeat no-op save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire path-equivalent failed save repeat no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire path-equivalent failed save repeat no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire path-equivalent failed save repeat no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire path-equivalent failed save repeat no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire path-equivalent failed save repeat no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire path-equivalent failed save repeat no-op save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire path-equivalent failed save repeat no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire path-equivalent failed save repeat no-op save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire path-equivalent failed save repeat no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire path-equivalent failed save repeat no-op output should match the first no-op output");
    check_reopened_shift_output(second_noop_output,
        "shift reacquire path-equivalent failed save repeat no-op save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire path-equivalent failed save repeat no-op save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire path-equivalent failed save repeat no-op save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire path-equivalent failed save repeat no-op save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire path-equivalent failed save repeat no-op save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire path-equivalent failed save repeat no-op save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire path-equivalent failed save repeat no-op save reopened output");
        });

    after_retry.set_cell("C3", fastxlsx::CellValue::text("post-noop-path-equivalent-failed-save"));
    check(after_retry.has_pending_changes() && sheet.has_pending_changes() &&
            reacquired.has_pending_changes(),
        "shift reacquire path-equivalent failed save post-noop edit should dirty all shared handles");
    check(after_retry.cell_count() == 4 && sheet.cell_count() == 4 &&
            reacquired.cell_count() == 4,
        "shift reacquire path-equivalent failed save post-noop edit should add one sparse cell on all handles");
    check_cell_range_equals(after_retry.used_range(), 1, 1, 3, 3,
        "shift reacquire path-equivalent failed save post-noop edit should keep combined bounds");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-path-equivalent-failed-save",
        "shift reacquire path-equivalent failed save post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, after_retry, 2, "shift reacquire path-equivalent failed save post-noop edit");

    editor.save_as(post_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire path-equivalent failed save post-noop save should clean all shared handles");
    check(editor.pending_change_count() == 3,
        "shift reacquire path-equivalent failed save post-noop save should record the third materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire path-equivalent failed save post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire path-equivalent failed save post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire path-equivalent failed save post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire path-equivalent failed save post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire path-equivalent failed save post-noop save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire path-equivalent failed save post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire path-equivalent failed save post-noop save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire path-equivalent failed save post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire path-equivalent failed save post-noop save should leave the repeat no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<c r="C3")",
        "shift reacquire path-equivalent failed save post-noop save should write the post-noop C3 cell");
    check_reopened_shift_output(post_noop_output,
        "shift reacquire path-equivalent failed save post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire path-equivalent failed save post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire path-equivalent failed save post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire path-equivalent failed save post-noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire path-equivalent failed save post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-path-equivalent-failed-save",
                "shift reacquire path-equivalent failed save post-noop save reopened output should keep post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 3 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "post-noop-path-equivalent-failed-save",
                "shift reacquire path-equivalent failed save post-noop row_cells should expose row-three order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 2 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_three[0].value.number_value() == 1.0 &&
                    column_three[1].reference.row == 3 &&
                    column_three[1].reference.column == 3 &&
                    column_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[1].value.text_value() == "post-noop-path-equivalent-failed-save",
                "shift reacquire path-equivalent failed save post-noop column_cells should expose shifted number and later edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire path-equivalent failed save post-noop save reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_empty_output_failed_save_preserves_dirty_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-empty-output-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-empty-output-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-empty-output-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-empty-output-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-empty-output-noop-repeat-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-empty-output-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire empty-output failed save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire empty-output failed save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire empty-output failed save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire empty-output failed save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire empty-output failed save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire empty-output failed save matching reacquire should stay clean before the later shift");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire empty-output failed save matching reacquire should reuse saved shifted state");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire empty-output failed save dirty state before rejected save",
        1,
        3,
        shifted_memory);

    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path()); }),
        "shift reacquire empty-output failed save should reject empty output path");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire empty-output failed save rejected empty output",
        1,
        3,
        shifted_memory);
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire empty-output failed save should preserve shifted numeric cells after rejection");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire empty-output failed save should preserve shifted source rows after rejection");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() && !reacquired.try_cell("A2").has_value(),
        "shift reacquire empty-output failed save should keep old sparse coordinates absent after rejection");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_xml, R"(<dimension ref="A1:B2"/>)",
        "shift reacquire empty-output failed save should leave the source workbook bounds unchanged");
    check_contains(source_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire empty-output failed save should leave the source workbook B1 unchanged");
    check_contains(source_xml, R"(<c r="A2")",
        "shift reacquire empty-output failed save should leave the source workbook A2 unchanged");
    check_not_contains(source_xml, R"(r="A3")",
        "shift reacquire empty-output failed save should not write the row shift into the source workbook");
    check_not_contains(source_xml, R"(r="C1")",
        "shift reacquire empty-output failed save should not write the column shift into the source workbook");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire empty-output failed save first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire empty-output failed save first output should keep B1 before rejected later shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire empty-output failed save first output should contain the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire empty-output failed save first output should not include the rejected later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire empty-output failed save first output should keep old row coordinate absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire empty-output failed save safe retry should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire empty-output failed save safe retry should clear dirty diagnostics");
    check(editor.pending_change_count() == 2,
        "shift reacquire empty-output failed save safe retry should record the later handoff");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire empty-output failed save safe retry should keep diagnostics clear");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire empty-output failed save safe retry should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire empty-output failed save safe retry should include shifted B1");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire empty-output failed save safe retry should retain the shifted source row");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire empty-output failed save safe retry should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire empty-output failed save safe retry should keep old row coordinate absent");

    check_reopened_shift_output(second_output, "shift reacquire empty-output failed save safe retry",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire empty-output failed save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire empty-output failed save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire empty-output failed save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire empty-output failed save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire empty-output failed save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire empty-output failed save reopened output");
        });

    fastxlsx::WorksheetEditor after_retry = editor.worksheet("Data");
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire empty-output failed save matching reacquire after retry should stay clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire empty-output failed save matching reacquire after retry should not add handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire empty-output failed save matching reacquire after retry should keep diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire empty-output failed save matching reacquire after retry should keep diagnostics clear");
    check(after_retry.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire empty-output failed save matching reacquire after retry should preserve shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire empty-output failed save matching reacquire after retry should expose shifted number");
    check(!after_retry.try_cell("B1").has_value() &&
            !after_retry.try_cell("A2").has_value(),
        "shift reacquire empty-output failed save matching reacquire after retry should keep old coordinates absent");
    check_shift_reacquire_retry_snapshots(
        sheet,
        "shift reacquire empty-output failed save matching reacquire after retry original handle");
    check_shift_reacquire_retry_snapshots(
        reacquired,
        "shift reacquire empty-output failed save matching reacquire after retry reacquired handle");
    check_shift_reacquire_retry_snapshots(
        after_retry,
        "shift reacquire empty-output failed save matching reacquire after retry clean handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire empty-output failed save noop save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire empty-output failed save noop save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire empty-output failed save noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire empty-output failed save noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire empty-output failed save noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire empty-output failed save noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire empty-output failed save noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "shift reacquire empty-output failed save noop output should match the safe retry output");
    check_reopened_shift_output(noop_output, "shift reacquire empty-output failed save noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire empty-output failed save noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire empty-output failed save noop save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire empty-output failed save noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire empty-output failed save noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire empty-output failed save noop save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire empty-output failed save noop save reopened output");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire empty-output failed save repeat no-op save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire empty-output failed save repeat no-op save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire empty-output failed save repeat no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire empty-output failed save repeat no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire empty-output failed save repeat no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire empty-output failed save repeat no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire empty-output failed save repeat no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire empty-output failed save repeat no-op save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire empty-output failed save repeat no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire empty-output failed save repeat no-op save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire empty-output failed save repeat no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire empty-output failed save repeat no-op output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "shift reacquire empty-output failed save repeat no-op save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire empty-output failed save repeat no-op save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire empty-output failed save repeat no-op save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire empty-output failed save repeat no-op save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire empty-output failed save repeat no-op save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire empty-output failed save repeat no-op save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire empty-output failed save repeat no-op save reopened output");
        });

    after_retry.set_cell("C3", fastxlsx::CellValue::text("post-noop-empty-output-failed-save"));
    check(after_retry.has_pending_changes() && sheet.has_pending_changes() &&
            reacquired.has_pending_changes(),
        "shift reacquire empty-output failed save post-noop edit should dirty all shared handles");
    check(after_retry.cell_count() == 4 && sheet.cell_count() == 4 &&
            reacquired.cell_count() == 4,
        "shift reacquire empty-output failed save post-noop edit should add one sparse cell on all handles");
    check_cell_range_equals(after_retry.used_range(), 1, 1, 3, 3,
        "shift reacquire empty-output failed save post-noop edit should keep combined bounds");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-empty-output-failed-save",
        "shift reacquire empty-output failed save post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, after_retry, 2, "shift reacquire empty-output failed save post-noop edit");

    editor.save_as(post_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire empty-output failed save post-noop save should clean all shared handles");
    check(editor.pending_change_count() == 3,
        "shift reacquire empty-output failed save post-noop save should record the third materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire empty-output failed save post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire empty-output failed save post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire empty-output failed save post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire empty-output failed save post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire empty-output failed save post-noop save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire empty-output failed save post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire empty-output failed save post-noop save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire empty-output failed save post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire empty-output failed save post-noop save should leave the repeat no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<c r="C3")",
        "shift reacquire empty-output failed save post-noop save should write the post-noop C3 cell");
    check_reopened_shift_output(post_noop_output, "shift reacquire empty-output failed save post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire empty-output failed save post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire empty-output failed save post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire empty-output failed save post-noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire empty-output failed save post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-empty-output-failed-save",
                "shift reacquire empty-output failed save post-noop save reopened output should keep post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 3 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "post-noop-empty-output-failed-save",
                "shift reacquire empty-output failed save post-noop row_cells should expose row-three order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 2 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_three[0].value.number_value() == 1.0 &&
                    column_three[1].reference.row == 3 &&
                    column_three[1].reference.column == 3 &&
                    column_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[1].value.text_value() == "post-noop-empty-output-failed-save",
                "shift reacquire empty-output failed save post-noop column_cells should expose shifted number and later edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire empty-output failed save post-noop save reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_missing_parent_failed_save_preserves_dirty_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-parent-source.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-parent-output") /
        "out.xlsx";
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-parent-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-parent-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-parent-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-parent-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-missing-parent-post-noop-output.xlsx");
    std::filesystem::remove_all(missing_parent_output.parent_path());

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire missing-parent failed save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire missing-parent failed save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire missing-parent failed save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire missing-parent failed save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing-parent failed save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire missing-parent failed save matching reacquire should stay clean before the later shift");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire missing-parent failed save matching reacquire should reuse saved shifted state");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire missing-parent failed save dirty state before rejected save",
        1,
        3,
        shifted_memory);

    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "shift reacquire missing-parent failed save should reject missing output parent");
    check(!std::filesystem::exists(missing_parent_output),
        "shift reacquire missing-parent failed save should not create the rejected output");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire missing-parent failed save rejected missing parent",
        1,
        3,
        shifted_memory);
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire missing-parent failed save should preserve shifted numeric cells after rejection");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire missing-parent failed save should preserve shifted source rows after rejection");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() && !reacquired.try_cell("A2").has_value(),
        "shift reacquire missing-parent failed save should keep old sparse coordinates absent after rejection");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_xml, R"(<dimension ref="A1:B2"/>)",
        "shift reacquire missing-parent failed save should leave the source workbook bounds unchanged");
    check_contains(source_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire missing-parent failed save should leave the source workbook B1 unchanged");
    check_contains(source_xml, R"(<c r="A2")",
        "shift reacquire missing-parent failed save should leave the source workbook A2 unchanged");
    check_not_contains(source_xml, R"(r="A3")",
        "shift reacquire missing-parent failed save should not write the row shift into the source workbook");
    check_not_contains(source_xml, R"(r="C1")",
        "shift reacquire missing-parent failed save should not write the column shift into the source workbook");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire missing-parent failed save first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire missing-parent failed save first output should keep B1 before rejected later shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire missing-parent failed save first output should contain the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire missing-parent failed save first output should not include the rejected later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire missing-parent failed save first output should keep old row coordinate absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire missing-parent failed save safe retry should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire missing-parent failed save safe retry should clear dirty diagnostics");
    check(editor.pending_change_count() == 2,
        "shift reacquire missing-parent failed save safe retry should record the later handoff");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing-parent failed save safe retry should keep diagnostics clear");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire missing-parent failed save safe retry should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire missing-parent failed save safe retry should include shifted B1");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire missing-parent failed save safe retry should retain the shifted source row");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire missing-parent failed save safe retry should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire missing-parent failed save safe retry should keep old row coordinate absent");

    check_reopened_shift_output(second_output, "shift reacquire missing-parent failed save safe retry",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire missing-parent failed save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire missing-parent failed save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire missing-parent failed save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire missing-parent failed save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire missing-parent failed save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire missing-parent failed save reopened output");
        });

    fastxlsx::WorksheetEditor after_retry = editor.worksheet("Data");
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire missing-parent failed save matching reacquire after retry should stay clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire missing-parent failed save matching reacquire after retry should not add handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire missing-parent failed save matching reacquire after retry should keep diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing-parent failed save matching reacquire after retry should keep diagnostics clear");
    check(after_retry.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire missing-parent failed save matching reacquire after retry should preserve shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire missing-parent failed save matching reacquire after retry should expose shifted number");
    check(!after_retry.try_cell("B1").has_value() &&
            !after_retry.try_cell("A2").has_value(),
        "shift reacquire missing-parent failed save matching reacquire after retry should keep old coordinates absent");
    check_shift_reacquire_retry_snapshots(
        sheet,
        "shift reacquire missing-parent failed save matching reacquire after retry original handle");
    check_shift_reacquire_retry_snapshots(
        reacquired,
        "shift reacquire missing-parent failed save matching reacquire after retry reacquired handle");
    check_shift_reacquire_retry_snapshots(
        after_retry,
        "shift reacquire missing-parent failed save matching reacquire after retry clean handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire missing-parent failed save noop save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire missing-parent failed save noop save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire missing-parent failed save noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire missing-parent failed save noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing-parent failed save noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire missing-parent failed save noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire missing-parent failed save noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "shift reacquire missing-parent failed save noop output should match the safe retry output");
    check_reopened_shift_output(noop_output, "shift reacquire missing-parent failed save noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire missing-parent failed save noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire missing-parent failed save noop save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire missing-parent failed save noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire missing-parent failed save noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire missing-parent failed save noop save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire missing-parent failed save noop save reopened output");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire missing-parent failed save repeat no-op save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire missing-parent failed save repeat no-op save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire missing-parent failed save repeat no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire missing-parent failed save repeat no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing-parent failed save repeat no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire missing-parent failed save repeat no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire missing-parent failed save repeat no-op save");
    check(!std::filesystem::exists(missing_parent_output),
        "shift reacquire missing-parent failed save repeat no-op save should not create the rejected output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire missing-parent failed save repeat no-op save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire missing-parent failed save repeat no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire missing-parent failed save repeat no-op save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire missing-parent failed save repeat no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire missing-parent failed save repeat no-op output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "shift reacquire missing-parent failed save repeat no-op save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire missing-parent failed save repeat no-op save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire missing-parent failed save repeat no-op save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire missing-parent failed save repeat no-op save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire missing-parent failed save repeat no-op save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire missing-parent failed save repeat no-op save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire missing-parent failed save repeat no-op save reopened output");
        });

    after_retry.set_cell("C3", fastxlsx::CellValue::text("post-noop-missing-parent-failed-save"));
    check(after_retry.has_pending_changes() && sheet.has_pending_changes() &&
            reacquired.has_pending_changes(),
        "shift reacquire missing-parent failed save post-noop edit should dirty all shared handles");
    check(after_retry.cell_count() == 4 && sheet.cell_count() == 4 &&
            reacquired.cell_count() == 4,
        "shift reacquire missing-parent failed save post-noop edit should add one sparse cell on all handles");
    check_cell_range_equals(after_retry.used_range(), 1, 1, 3, 3,
        "shift reacquire missing-parent failed save post-noop edit should keep combined bounds");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-missing-parent-failed-save",
        "shift reacquire missing-parent failed save post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, after_retry, 2, "shift reacquire missing-parent failed save post-noop edit");

    editor.save_as(post_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire missing-parent failed save post-noop save should clean all shared handles");
    check(editor.pending_change_count() == 3,
        "shift reacquire missing-parent failed save post-noop save should record the third materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire missing-parent failed save post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire missing-parent failed save post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire missing-parent failed save post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire missing-parent failed save post-noop save should keep diagnostics clear");
    check(!std::filesystem::exists(missing_parent_output),
        "shift reacquire missing-parent failed save post-noop save should not create the rejected output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire missing-parent failed save post-noop save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire missing-parent failed save post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire missing-parent failed save post-noop save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire missing-parent failed save post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire missing-parent failed save post-noop save should leave the repeat no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<c r="C3")",
        "shift reacquire missing-parent failed save post-noop save should write the post-noop C3 cell");
    check_reopened_shift_output(post_noop_output, "shift reacquire missing-parent failed save post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire missing-parent failed save post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire missing-parent failed save post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire missing-parent failed save post-noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire missing-parent failed save post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-missing-parent-failed-save",
                "shift reacquire missing-parent failed save post-noop save reopened output should keep post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 3 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "post-noop-missing-parent-failed-save",
                "shift reacquire missing-parent failed save post-noop row_cells should expose row-three order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 2 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_three[0].value.number_value() == 1.0 &&
                    column_three[1].reference.row == 3 &&
                    column_three[1].reference.column == 3 &&
                    column_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[1].value.text_value() == "post-noop-missing-parent-failed-save",
                "shift reacquire missing-parent failed save post-noop column_cells should expose shifted number and later edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire missing-parent failed save post-noop save reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_non_directory_parent_failed_save_preserves_dirty_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-file-parent-source.xlsx");
    const std::filesystem::path file_parent =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-file-parent");
    const std::filesystem::path non_directory_output = file_parent / "out.xlsx";
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-file-parent-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-file-parent-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-file-parent-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-file-parent-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-file-parent-post-noop-output.xlsx");
    std::filesystem::remove_all(file_parent);
    fastxlsx::test::write_file(file_parent, "not a directory");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire file-parent failed save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire file-parent failed save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire file-parent failed save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire file-parent failed save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire file-parent failed save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire file-parent failed save matching reacquire should stay clean before the later shift");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire file-parent failed save matching reacquire should reuse saved shifted state");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire file-parent failed save dirty state before rejected save",
        1,
        3,
        shifted_memory);

    check(threw_fastxlsx_error([&] { editor.save_as(non_directory_output); }),
        "shift reacquire file-parent failed save should reject non-directory output parent");
    check(std::filesystem::is_regular_file(file_parent) &&
            fastxlsx::test::read_file(file_parent) == "not a directory",
        "shift reacquire file-parent failed save should preserve the non-directory parent file");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire file-parent failed save rejected non-directory parent",
        1,
        3,
        shifted_memory);
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire file-parent failed save should preserve shifted numeric cells after rejection");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire file-parent failed save should preserve shifted source rows after rejection");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() && !reacquired.try_cell("A2").has_value(),
        "shift reacquire file-parent failed save should keep old sparse coordinates absent after rejection");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_xml, R"(<dimension ref="A1:B2"/>)",
        "shift reacquire file-parent failed save should leave the source workbook bounds unchanged");
    check_contains(source_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire file-parent failed save should leave the source workbook B1 unchanged");
    check_contains(source_xml, R"(<c r="A2")",
        "shift reacquire file-parent failed save should leave the source workbook A2 unchanged");
    check_not_contains(source_xml, R"(r="A3")",
        "shift reacquire file-parent failed save should not write the row shift into the source workbook");
    check_not_contains(source_xml, R"(r="C1")",
        "shift reacquire file-parent failed save should not write the column shift into the source workbook");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire file-parent failed save first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire file-parent failed save first output should keep B1 before rejected later shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire file-parent failed save first output should contain the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire file-parent failed save first output should not include the rejected later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire file-parent failed save first output should keep old row coordinate absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire file-parent failed save safe retry should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire file-parent failed save safe retry should clear dirty diagnostics");
    check(editor.pending_change_count() == 2,
        "shift reacquire file-parent failed save safe retry should record the later handoff");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire file-parent failed save safe retry should keep diagnostics clear");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire file-parent failed save safe retry should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire file-parent failed save safe retry should include shifted B1");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire file-parent failed save safe retry should retain the shifted source row");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire file-parent failed save safe retry should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire file-parent failed save safe retry should keep old row coordinate absent");

    check_reopened_shift_output(second_output, "shift reacquire file-parent failed save safe retry",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire file-parent failed save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire file-parent failed save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire file-parent failed save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire file-parent failed save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire file-parent failed save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire file-parent failed save reopened output");
        });

    fastxlsx::WorksheetEditor after_retry = editor.worksheet("Data");
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire file-parent failed save matching reacquire after retry should stay clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire file-parent failed save matching reacquire after retry should not add handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire file-parent failed save matching reacquire after retry should keep diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire file-parent failed save matching reacquire after retry should keep diagnostics clear");
    check(after_retry.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire file-parent failed save matching reacquire after retry should preserve shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire file-parent failed save matching reacquire after retry should expose shifted number");
    check(!after_retry.try_cell("B1").has_value() &&
            !after_retry.try_cell("A2").has_value(),
        "shift reacquire file-parent failed save matching reacquire after retry should keep old coordinates absent");
    check_shift_reacquire_retry_snapshots(
        sheet,
        "shift reacquire file-parent failed save matching reacquire after retry original handle");
    check_shift_reacquire_retry_snapshots(
        reacquired,
        "shift reacquire file-parent failed save matching reacquire after retry reacquired handle");
    check_shift_reacquire_retry_snapshots(
        after_retry,
        "shift reacquire file-parent failed save matching reacquire after retry clean handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire file-parent failed save noop save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire file-parent failed save noop save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire file-parent failed save noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire file-parent failed save noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire file-parent failed save noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift reacquire file-parent failed save noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift reacquire file-parent failed save noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "shift reacquire file-parent failed save noop output should match the safe retry output");
    check_reopened_shift_output(noop_output, "shift reacquire file-parent failed save noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire file-parent failed save noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire file-parent failed save noop save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire file-parent failed save noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire file-parent failed save noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire file-parent failed save noop save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire file-parent failed save noop save reopened output");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire file-parent failed save repeat no-op save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire file-parent failed save repeat no-op save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire file-parent failed save repeat no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire file-parent failed save repeat no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire file-parent failed save repeat no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift reacquire file-parent failed save repeat no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift reacquire file-parent failed save repeat no-op save");
    check(std::filesystem::is_regular_file(file_parent) &&
            fastxlsx::test::read_file(file_parent) == "not a directory",
        "shift reacquire file-parent failed save repeat no-op save should preserve the non-directory parent file");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire file-parent failed save repeat no-op save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire file-parent failed save repeat no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire file-parent failed save repeat no-op save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire file-parent failed save repeat no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire file-parent failed save repeat no-op output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "shift reacquire file-parent failed save repeat no-op save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire file-parent failed save repeat no-op save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire file-parent failed save repeat no-op save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire file-parent failed save repeat no-op save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire file-parent failed save repeat no-op save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire file-parent failed save repeat no-op save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire file-parent failed save repeat no-op save reopened output");
        });

    after_retry.set_cell("C3", fastxlsx::CellValue::text("post-noop-file-parent-failed-save"));
    check(after_retry.has_pending_changes() && sheet.has_pending_changes() &&
            reacquired.has_pending_changes(),
        "shift reacquire file-parent failed save post-noop edit should dirty all shared handles");
    check(after_retry.cell_count() == 4 && sheet.cell_count() == 4 &&
            reacquired.cell_count() == 4,
        "shift reacquire file-parent failed save post-noop edit should add one sparse cell on all handles");
    check_cell_range_equals(after_retry.used_range(), 1, 1, 3, 3,
        "shift reacquire file-parent failed save post-noop edit should keep combined bounds");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-file-parent-failed-save",
        "shift reacquire file-parent failed save post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, after_retry, 2, "shift reacquire file-parent failed save post-noop edit");

    editor.save_as(post_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire file-parent failed save post-noop save should clean all shared handles");
    check(editor.pending_change_count() == 3,
        "shift reacquire file-parent failed save post-noop save should record the third materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire file-parent failed save post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire file-parent failed save post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire file-parent failed save post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire file-parent failed save post-noop save should keep diagnostics clear");
    check(std::filesystem::is_regular_file(file_parent) &&
            fastxlsx::test::read_file(file_parent) == "not a directory",
        "shift reacquire file-parent failed save post-noop save should preserve the non-directory parent file");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire file-parent failed save post-noop save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire file-parent failed save post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire file-parent failed save post-noop save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire file-parent failed save post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire file-parent failed save post-noop save should leave the repeat no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<c r="C3")",
        "shift reacquire file-parent failed save post-noop save should write the post-noop C3 cell");
    check_reopened_shift_output(post_noop_output, "shift reacquire file-parent failed save post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire file-parent failed save post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire file-parent failed save post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire file-parent failed save post-noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire file-parent failed save post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-file-parent-failed-save",
                "shift reacquire file-parent failed save post-noop save reopened output should keep post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 3 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "post-noop-file-parent-failed-save",
                "shift reacquire file-parent failed save post-noop row_cells should expose row-three order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 2 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_three[0].value.number_value() == 1.0 &&
                    column_three[1].reference.row == 3 &&
                    column_three[1].reference.column == 3 &&
                    column_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[1].value.text_value() == "post-noop-file-parent-failed-save",
                "shift reacquire file-parent failed save post-noop column_cells should expose shifted number and later edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire file-parent failed save post-noop save reopened output should keep old coordinates absent");
        });
}

void test_public_worksheet_editor_shift_reacquire_existing_directory_failed_save_preserves_dirty_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-directory-output-source.xlsx");
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-directory-output");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-directory-output-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-directory-output-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-directory-output-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-directory-output-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-reacquire-directory-output-post-noop-output.xlsx");
    std::filesystem::remove_all(directory_output);
    std::filesystem::create_directories(directory_output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "shift reacquire directory-output failed save pre-save shift");
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "shift reacquire directory-output failed save first save should clean the borrowed handle");
    check(editor.pending_change_count() == 1,
        "shift reacquire directory-output failed save first save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire directory-output failed save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire directory-output failed save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "shift reacquire directory-output failed save matching reacquire should stay clean before the later shift");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire directory-output failed save matching reacquire should reuse saved shifted state");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire directory-output failed save dirty state before rejected save",
        1,
        3,
        shifted_memory);

    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "shift reacquire directory-output failed save should reject existing directory output");
    check(std::filesystem::is_directory(directory_output),
        "shift reacquire directory-output failed save should preserve the rejected output directory");
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "Missing",
        "shift reacquire directory-output failed save rejected existing directory",
        1,
        3,
        shifted_memory);
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire directory-output failed save should preserve shifted numeric cells after rejection");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire directory-output failed save should preserve shifted source rows after rejection");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() && !reacquired.try_cell("A2").has_value(),
        "shift reacquire directory-output failed save should keep old sparse coordinates absent after rejection");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_xml, R"(<dimension ref="A1:B2"/>)",
        "shift reacquire directory-output failed save should leave the source workbook bounds unchanged");
    check_contains(source_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire directory-output failed save should leave the source workbook B1 unchanged");
    check_contains(source_xml, R"(<c r="A2")",
        "shift reacquire directory-output failed save should leave the source workbook A2 unchanged");
    check_not_contains(source_xml, R"(r="A3")",
        "shift reacquire directory-output failed save should not write the row shift into the source workbook");
    check_not_contains(source_xml, R"(r="C1")",
        "shift reacquire directory-output failed save should not write the column shift into the source workbook");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<dimension ref="A1:B3"/>)",
        "shift reacquire directory-output failed save first output should keep shifted row bounds");
    check_contains(first_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift reacquire directory-output failed save first output should keep B1 before rejected later shift");
    check_contains(first_xml, R"(<c r="A3")",
        "shift reacquire directory-output failed save first output should contain the shifted source row");
    check_not_contains(first_xml, R"(r="C1")",
        "shift reacquire directory-output failed save first output should not include the rejected later column shift");
    check_not_contains(first_xml, R"(r="A2")",
        "shift reacquire directory-output failed save first output should keep old row coordinate absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "shift reacquire directory-output failed save safe retry should clean both handles");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift reacquire directory-output failed save safe retry should clear dirty diagnostics");
    check(editor.pending_change_count() == 2,
        "shift reacquire directory-output failed save safe retry should record the later handoff");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire directory-output failed save safe retry should keep diagnostics clear");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_xml, R"(<dimension ref="A1:C3"/>)",
        "shift reacquire directory-output failed save safe retry should project combined shifted bounds");
    check_contains(second_xml, R"(<c r="C1"><v>1</v></c>)",
        "shift reacquire directory-output failed save safe retry should include shifted B1");
    check_contains(second_xml, R"(<c r="A3")",
        "shift reacquire directory-output failed save safe retry should retain the shifted source row");
    check_not_contains(second_xml, R"(r="B1")",
        "shift reacquire directory-output failed save safe retry should omit old B1");
    check_not_contains(second_xml, R"(r="A2")",
        "shift reacquire directory-output failed save safe retry should keep old row coordinate absent");

    check_reopened_shift_output(second_output, "shift reacquire directory-output failed save safe retry",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire directory-output failed save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire directory-output failed save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire directory-output failed save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire directory-output failed save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire directory-output failed save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire directory-output failed save reopened output");
        });

    fastxlsx::WorksheetEditor after_retry = editor.worksheet("Data");
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire directory-output failed save matching reacquire after retry should stay clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire directory-output failed save matching reacquire after retry should not add handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire directory-output failed save matching reacquire after retry should keep diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire directory-output failed save matching reacquire after retry should keep diagnostics clear");
    check(after_retry.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "shift reacquire directory-output failed save matching reacquire after retry should preserve shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "shift reacquire directory-output failed save matching reacquire after retry should expose shifted number");
    check(!after_retry.try_cell("B1").has_value() &&
            !after_retry.try_cell("A2").has_value(),
        "shift reacquire directory-output failed save matching reacquire after retry should keep old coordinates absent");
    check_shift_reacquire_retry_snapshots(
        sheet,
        "shift reacquire directory-output failed save matching reacquire after retry original handle");
    check_shift_reacquire_retry_snapshots(
        reacquired,
        "shift reacquire directory-output failed save matching reacquire after retry reacquired handle");
    check_shift_reacquire_retry_snapshots(
        after_retry,
        "shift reacquire directory-output failed save matching reacquire after retry clean handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire directory-output failed save noop save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire directory-output failed save noop save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire directory-output failed save noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire directory-output failed save noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire directory-output failed save noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "shift reacquire directory-output failed save noop save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "shift reacquire directory-output failed save noop save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "shift reacquire directory-output failed save noop output should match the safe retry output");
    check_reopened_shift_output(noop_output, "shift reacquire directory-output failed save noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire directory-output failed save noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire directory-output failed save noop save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire directory-output failed save noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire directory-output failed save noop save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire directory-output failed save noop save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire directory-output failed save noop save reopened output");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire directory-output failed save repeat no-op save should keep all handles clean");
    check(editor.pending_change_count() == 2,
        "shift reacquire directory-output failed save repeat no-op save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire directory-output failed save repeat no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire directory-output failed save repeat no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire directory-output failed save repeat no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "shift reacquire directory-output failed save repeat no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "shift reacquire directory-output failed save repeat no-op save");
    check(std::filesystem::is_directory(directory_output),
        "shift reacquire directory-output failed save repeat no-op save should preserve the rejected output directory");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire directory-output failed save repeat no-op save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire directory-output failed save repeat no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire directory-output failed save repeat no-op save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire directory-output failed save repeat no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift reacquire directory-output failed save repeat no-op output should match the first no-op output");
    check_reopened_shift_output(second_noop_output, "shift reacquire directory-output failed save repeat no-op save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "shift reacquire directory-output failed save repeat no-op save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire directory-output failed save repeat no-op save reopened output should expose combined bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire directory-output failed save repeat no-op save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire directory-output failed save repeat no-op save reopened output should keep shifted A2");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire directory-output failed save repeat no-op save reopened output should keep old coordinates absent");
            check_shift_reacquire_retry_snapshots(reopened_sheet,
                "shift reacquire directory-output failed save repeat no-op save reopened output");
        });

    after_retry.set_cell("C3", fastxlsx::CellValue::text("post-noop-directory-output-failed-save"));
    check(after_retry.has_pending_changes() && sheet.has_pending_changes() &&
            reacquired.has_pending_changes(),
        "shift reacquire directory-output failed save post-noop edit should dirty all shared handles");
    check(after_retry.cell_count() == 4 && sheet.cell_count() == 4 &&
            reacquired.cell_count() == 4,
        "shift reacquire directory-output failed save post-noop edit should add one sparse cell on all handles");
    check_cell_range_equals(after_retry.used_range(), 1, 1, 3, 3,
        "shift reacquire directory-output failed save post-noop edit should keep combined bounds");
    const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
    check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_cell.text_value() == "post-noop-directory-output-failed-save",
        "shift reacquire directory-output failed save post-noop edit should be visible through the older handle");
    check_public_state_single_data_dirty_materialized_summary(
        editor, after_retry, 2, "shift reacquire directory-output failed save post-noop edit");

    editor.save_as(post_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "shift reacquire directory-output failed save post-noop save should clean all shared handles");
    check(editor.pending_change_count() == 3,
        "shift reacquire directory-output failed save post-noop save should record the third materialized handoff");
    check(editor.has_pending_changes(),
        "shift reacquire directory-output failed save post-noop save should retain staged materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "shift reacquire directory-output failed save post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "shift reacquire directory-output failed save post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "shift reacquire directory-output failed save post-noop save should keep diagnostics clear");
    check(std::filesystem::is_directory(directory_output),
        "shift reacquire directory-output failed save post-noop save should preserve the rejected output directory");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift reacquire directory-output failed save post-noop save should leave the source unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "shift reacquire directory-output failed save post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "shift reacquire directory-output failed save post-noop save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift reacquire directory-output failed save post-noop save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "shift reacquire directory-output failed save post-noop save should leave the repeat no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<c r="C3")",
        "shift reacquire directory-output failed save post-noop save should write the post-noop C3 cell");
    check_reopened_shift_output(post_noop_output, "shift reacquire directory-output failed save post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "shift reacquire directory-output failed save post-noop save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "shift reacquire directory-output failed save post-noop save reopened output should expose post-noop bounds");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 1.0,
                "shift reacquire directory-output failed save post-noop save reopened output should read shifted B1");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a3.text_value() == "placeholder-a2",
                "shift reacquire directory-output failed save post-noop save reopened output should keep shifted A2");
            const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
            check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c3.text_value() == "post-noop-directory-output-failed-save",
                "shift reacquire directory-output failed save post-noop save reopened output should keep post-noop edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 3 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "post-noop-directory-output-failed-save",
                "shift reacquire directory-output failed save post-noop row_cells should expose row-three order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 2 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_three[0].value.number_value() == 1.0 &&
                    column_three[1].reference.row == 3 &&
                    column_three[1].reference.column == 3 &&
                    column_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_three[1].value.text_value() == "post-noop-directory-output-failed-save",
                "shift reacquire directory-output failed save post-noop column_cells should expose shifted number and later edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value(),
                "shift reacquire directory-output failed save post-noop save reopened output should keep old coordinates absent");
        });
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_shift_reacquire_failed_save_preserves_dirty_session();
        test_public_worksheet_editor_shift_reacquire_after_failed_save_retry_reuses_session();
        test_public_worksheet_editor_shift_reacquire_after_failed_save_retry_noop_save_preserves_session();
        test_public_worksheet_editor_shift_reacquire_path_equivalent_failed_save_preserves_dirty_session();
        test_public_worksheet_editor_shift_reacquire_empty_output_failed_save_preserves_dirty_session();
        test_public_worksheet_editor_shift_reacquire_missing_parent_failed_save_preserves_dirty_session();
        test_public_worksheet_editor_shift_reacquire_non_directory_parent_failed_save_preserves_dirty_session();
        test_public_worksheet_editor_shift_reacquire_existing_directory_failed_save_preserves_dirty_session();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor public-state reacquire retry check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor public-state reacquire retry tests passed\n");
    return 0;
}
