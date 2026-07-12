#include "test_workbook_editor_public_state_reacquire_support.hpp"

namespace {

void test_public_worksheet_editor_shift_after_rename_reacquire_reuses_planned_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-reacquire-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-reacquire-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-reacquire-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-reacquire-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-reacquire-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-reacquire-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed shift reacquire first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed shift reacquire first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift reacquire first save should clear dirty materialized diagnostics");

    std::optional<fastxlsx::WorksheetEditor> maybe_reacquired =
        editor.try_worksheet("RenamedData");
    check(maybe_reacquired.has_value(),
        "renamed shift reacquire should find the planned-name saved session");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed shift reacquire should not find the old source sheet name");
    if (!maybe_reacquired.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor reacquired = std::move(*maybe_reacquired);
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed shift reacquire should return the saved clean materialized session");
    check(reacquired.name() == "RenamedData",
        "renamed shift reacquire should preserve the planned handle name");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift reacquire should reuse the saved shifted sparse state");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "renamed shift reacquire should keep old row coordinates absent");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed shift reacquire later shift should dirty the shared planned-name session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift reacquire later shift should report dirty state under the planned name");
    check(editor.pending_materialized_cell_count() == 3,
        "renamed shift reacquire later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed shift reacquire later shift should report the shared memory estimate");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "renamed shift reacquire later shift should expose one combined summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" &&
                    summaries[0].planned_name == "RenamedData" &&
                    summaries[0].renamed &&
                    summaries[0].materialized_dirty,
                "renamed shift reacquire later shift summary should retain rename and dirty state");
            check(summaries[0].materialized_cell_count == 3,
                "renamed shift reacquire later shift summary should report sparse count");
            check(summaries[0].estimated_materialized_memory_usage == shifted_memory,
                "renamed shift reacquire later shift summary should report memory estimate");
        }
    }
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "renamed shift reacquire later shift should move B1 and share it across handles");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value(),
        "renamed shift reacquire later shift should keep old column coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift reacquire second save should clean both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift reacquire second save should record the second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift reacquire second save should clear dirty materialized diagnostics");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed shift reacquire first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed shift reacquire first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "renamed shift reacquire first output should contain only the first shift");
    check_contains(first_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "renamed shift reacquire first output should keep B1 before later shift");
    check_contains(first_worksheet_xml, R"(<c r="A3")",
        "renamed shift reacquire first output should keep shifted A2");
    check_not_contains(first_worksheet_xml, R"(r="C1")",
        "renamed shift reacquire first output should not include the later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed shift reacquire second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed shift reacquire second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed shift reacquire second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "renamed shift reacquire second output should write shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed shift reacquire second output should retain shifted A2");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed shift reacquire second output should omit the old B1 coordinate");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed shift reacquire second output should omit the old A2 coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift reacquire no-op save should keep both planned-name handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift reacquire no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift reacquire no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed shift reacquire no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift reacquire no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed shift reacquire no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed shift reacquire no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed shift reacquire no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift reacquire no-op save should leave the source package unchanged");
    check_reopened_renamed_shift_noop_output(
        noop_output, "renamed shift reacquire no-op output");

    const auto noop_entries_before_second_noop = fastxlsx::test::read_zip_entries(noop_output);
    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift reacquire second no-op save should keep both planned-name handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift reacquire second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift reacquire second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed shift reacquire second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift reacquire second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "renamed shift reacquire second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "renamed shift reacquire second no-op save");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries_before_second_noop,
        "renamed shift reacquire second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries_before_second_noop,
        "renamed shift reacquire second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift reacquire second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift reacquire second no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift reacquire second no-op save should leave the second output unchanged");
    check_reopened_renamed_shift_noop_output(
        second_noop_output, "renamed shift reacquire second no-op output");

    reacquired.set_cell("D3", fastxlsx::CellValue::text("post-noop-renamed-reacquire"));
    const std::size_t post_noop_memory = reacquired.estimated_memory_usage();
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed shift reacquire post-noop edit should dirty both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift reacquire post-noop edit should not count dirty state as a saved handoff");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift reacquire post-noop edit should report dirty state under the planned name");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed shift reacquire post-noop edit should add one sparse cell");
    check(editor.estimated_pending_materialized_memory_usage() == post_noop_memory,
        "renamed shift reacquire post-noop edit should report the shared memory estimate");
    check(sheet.get_cell("D3").text_value() == "post-noop-renamed-reacquire" &&
            reacquired.get_cell("D3").text_value() == "post-noop-renamed-reacquire",
        "renamed shift reacquire post-noop edit should be visible through both handles");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "renamed shift reacquire post-noop edit should preserve shifted source cells");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor,
        sheet,
        reacquired,
        4,
        post_noop_memory,
        "renamed shift reacquire post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift reacquire post-noop save should clean both planned-name handles");
    check(editor.pending_change_count() == 4,
        "renamed shift reacquire post-noop save should record the third materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift reacquire post-noop save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed shift reacquire post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift reacquire post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift reacquire post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift reacquire post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift reacquire post-noop save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries_before_second_noop,
        "renamed shift reacquire post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "renamed shift reacquire post-noop save should leave the second no-op output unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_workbook_xml = post_noop_entries.at("xl/workbook.xml");
    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_workbook_xml, R"(name="RenamedData")",
        "renamed shift reacquire post-noop output should keep the planned catalog name");
    check_not_contains(post_noop_workbook_xml, R"(name="Data")",
        "renamed shift reacquire post-noop output should omit the source catalog name");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "renamed shift reacquire post-noop output should expand bounds to D3");
    check_contains(post_noop_worksheet_xml, R"(<c r="D3")",
        "renamed shift reacquire post-noop output should write the later D3 cell");
    check_contains(post_noop_worksheet_xml, "post-noop-renamed-reacquire",
        "renamed shift reacquire post-noop output should write the later D3 text");
    check_not_contains(post_noop_worksheet_xml, R"(r="B1")",
        "renamed shift reacquire post-noop output should keep old B1 absent");
    check_not_contains(post_noop_worksheet_xml, R"(r="A2")",
        "renamed shift reacquire post-noop output should keep old A2 absent");
    check_reopened_clean_sheet_output(
        post_noop_output, "RenamedData", "renamed shift reacquire post-noop output",
        [](fastxlsx::WorksheetEditor& post_noop_sheet) {
            check(post_noop_sheet.cell_count() == 4,
                "renamed shift reacquire post-noop output should keep sparse count");
            check_cell_range_equals(post_noop_sheet.used_range(), 1, 1, 3, 4,
                "renamed shift reacquire post-noop output should expose post-noop bounds");
            check(post_noop_sheet.get_cell("C1").number_value() == 1.0,
                "renamed shift reacquire post-noop output should read shifted B1");
            check(post_noop_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "renamed shift reacquire post-noop output should read shifted A2");
            check(post_noop_sheet.get_cell("D3").text_value() == "post-noop-renamed-reacquire",
                "renamed shift reacquire post-noop output should read the later D3 edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                post_noop_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 4 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "post-noop-renamed-reacquire",
                "renamed shift reacquire post-noop row_cells should expose shifted row order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                post_noop_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 3 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "post-noop-renamed-reacquire",
                "renamed shift reacquire post-noop column_cells should expose the later edit");
            check(!post_noop_sheet.try_cell("B1").has_value() &&
                    !post_noop_sheet.try_cell("A2").has_value(),
                "renamed shift reacquire post-noop output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed shift reacquire reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed shift reacquire reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift reacquire reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 3,
        "renamed shift reacquire reopened output should keep sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed shift reacquire reopened output should expose combined bounds");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0,
        "renamed shift reacquire reopened output should read shifted B1");
    check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift reacquire reopened output should read shifted A2");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("A2").has_value(),
        "renamed shift reacquire reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_failed_save_preserves_planned_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-failed-save-source.xlsx");
    const std::filesystem::path equivalent_source =
        source.parent_path() / "." / source.filename();
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-failed-save-missing-parent") /
        "out.xlsx";
    const std::filesystem::path file_parent =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-failed-save-file-parent");
    const std::filesystem::path non_directory_output = file_parent / "out.xlsx";
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-failed-save-directory-output");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-failed-save-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-failed-save-second-output.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-failed-save-third-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-failed-save-second-noop-output.xlsx");
    std::filesystem::remove_all(missing_parent_output.parent_path());
    std::filesystem::remove_all(file_parent);
    fastxlsx::test::write_file(file_parent, "not a directory");
    std::filesystem::remove_all(directory_output);
    std::filesystem::create_directories(directory_output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();

    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed shift failed save first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed shift failed save first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift failed save first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift failed save first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed shift failed save matching reacquire should reuse the clean planned session");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed shift failed save matching reacquire should keep the old source name unavailable");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift failed save matching reacquire should read the saved shifted row");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    const auto check_dirty_planned_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep last_edit_error clear");
        check(editor.has_pending_changes(),
            label + " should preserve dirty public facade state");
        check(editor.pending_change_count() == 2,
            label + " should keep the rename plus first materialized handoff count");
        check_workbook_editor_no_replacement_diagnostics(
            editor, label + " should not invent replacement diagnostics");
        check(!editor.has_pending_replacement("Data") &&
                !editor.has_pending_replacement("RenamedData"),
            label + " should not report replacement state for source or planned names");
        check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
            label + " should dirty both planned-name handles");
        check(sheet.cell_count() == 3 && reacquired.cell_count() == 3,
            label + " should expose the combined shifted sparse count on both handles");
        check(sheet.estimated_memory_usage() == shifted_memory &&
                reacquired.estimated_memory_usage() == shifted_memory,
            label + " should expose the same planned-session memory on both handles");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report the dirty materialized session under the planned name");
        check(editor.pending_materialized_cell_count() == 3,
            label + " should report the dirty planned-session sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should report the dirty planned-session memory");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                label + " should expose one renamed dirty summary");
            if (summaries.size() == 1) {
                check(summaries[0].source_name == "Data" &&
                        summaries[0].planned_name == "RenamedData" &&
                        summaries[0].renamed &&
                        summaries[0].materialized_dirty,
                    label + " summary should retain source/planned names and dirty state");
                check(summaries[0].materialized_cell_count == 3,
                    label + " summary should report the combined shifted sparse count");
                check(summaries[0].estimated_materialized_memory_usage == shifted_memory,
                    label + " summary should report the combined shifted memory");
            }
        }
        check(editor.source_worksheet_names() == expected_source_names,
            label + " should preserve source worksheet names");
        check(editor.worksheet_names() == expected_planned_names,
            label + " should preserve planned worksheet names");
        check(workbook_editor_catalog_entries_equal(
                  editor.worksheet_catalog(), expected_catalog),
            label + " should preserve the renamed worksheet catalog");
        check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
            label + " should keep only the planned sheet name available");
    };

    check_dirty_planned_session(
        "renamed shift failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "renamed shift failed save should reject exact source overwrite");
    check_dirty_planned_session(
        "renamed shift failed save rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(equivalent_source); }),
        "renamed shift failed save should reject path-equivalent source overwrite");
    check_dirty_planned_session(
        "renamed shift failed save rejected path-equivalent source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path()); }),
        "renamed shift failed save should reject empty output path");
    check_dirty_planned_session(
        "renamed shift failed save rejected empty output path");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "renamed shift failed save should reject missing output parent");
    check(!std::filesystem::exists(missing_parent_output),
        "renamed shift failed save should not create the rejected missing-parent output");
    check_dirty_planned_session(
        "renamed shift failed save rejected missing output parent");
    check(threw_fastxlsx_error([&] { editor.save_as(non_directory_output); }),
        "renamed shift failed save should reject non-directory output parent");
    check(std::filesystem::is_regular_file(file_parent) &&
            fastxlsx::test::read_file(file_parent) == "not a directory",
        "renamed shift failed save should preserve the non-directory parent file");
    check_dirty_planned_session(
        "renamed shift failed save rejected non-directory output parent");
    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "renamed shift failed save should reject existing directory output");
    check(std::filesystem::is_directory(directory_output),
        "renamed shift failed save should preserve the rejected output directory");
    check_dirty_planned_session(
        "renamed shift failed save rejected existing directory output");
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "renamed shift failed save should preserve shifted numeric cells after rejection");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift failed save should preserve shifted source rows after rejection");
    check(!sheet.try_cell("B1").has_value() && !reacquired.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() && !reacquired.try_cell("A2").has_value(),
        "renamed shift failed save should keep old sparse coordinates absent after rejection");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_workbook_xml = source_entries.at("xl/workbook.xml");
    const std::string source_worksheet_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_workbook_xml, R"(name="Data")",
        "renamed shift failed save should leave the source workbook catalog unchanged");
    check_not_contains(source_workbook_xml, R"(name="RenamedData")",
        "renamed shift failed save should not write the planned name into the source workbook");
    check_contains(source_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "renamed shift failed save should leave the source workbook bounds unchanged");
    check_contains(source_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "renamed shift failed save should leave the source workbook B1 unchanged");
    check_contains(source_worksheet_xml, R"(<c r="A2")",
        "renamed shift failed save should leave the source workbook A2 unchanged");
    check_not_contains(source_worksheet_xml, R"(r="A3")",
        "renamed shift failed save should not write the row shift into the source workbook");
    check_not_contains(source_worksheet_xml, R"(r="C1")",
        "renamed shift failed save should not write the column shift into the source workbook");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed shift failed save first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed shift failed save first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "renamed shift failed save first output should keep the row-shift-only bounds");
    check_contains(first_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "renamed shift failed save first output should keep B1 before the rejected later shift");
    check_contains(first_worksheet_xml, R"(<c r="A3")",
        "renamed shift failed save first output should contain the shifted source row");
    check_not_contains(first_worksheet_xml, R"(r="C1")",
        "renamed shift failed save first output should not include the later column shift");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift failed save safe retry should clean both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift failed save safe retry should record the second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift failed save safe retry should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift failed save safe retry should keep diagnostics clear");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed shift failed save safe retry should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed shift failed save safe retry should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed shift failed save safe retry should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "renamed shift failed save safe retry should write shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed shift failed save safe retry should retain shifted A2");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed shift failed save safe retry should omit old B1");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed shift failed save safe retry should keep old row coordinate absent");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed shift failed save reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed shift failed save reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift failed save reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 3,
        "renamed shift failed save reopened output should keep sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed shift failed save reopened output should expose combined bounds");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0,
        "renamed shift failed save reopened output should read shifted B1");
    check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift failed save reopened output should read shifted A2");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("A2").has_value(),
        "renamed shift failed save reopened output should keep old coordinates absent");

    std::optional<fastxlsx::WorksheetEditor> maybe_after_retry =
        editor.try_worksheet("RenamedData");
    check(maybe_after_retry.has_value(),
        "renamed shift failed save after retry should find the saved planned-name session");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed shift failed save after retry should keep the old source name unavailable");
    if (!maybe_after_retry.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor after_retry = std::move(*maybe_after_retry);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "renamed shift failed save after retry should return a clean saved session");
    check(editor.pending_change_count() == 3,
        "renamed shift failed save after retry clean reacquire should not add handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift failed save after retry clean reacquire should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift failed save after retry clean reacquire should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed shift failed save after retry clean reacquire should preserve catalog names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed shift failed save after retry clean reacquire should preserve the planned catalog");
    check(after_retry.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift failed save after retry should preserve the combined shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "renamed shift failed save after retry should expose the combined shifted number on all handles");
    check(!after_retry.try_cell("B1").has_value() &&
            !after_retry.try_cell("A2").has_value(),
        "renamed shift failed save after retry should keep old sparse coordinates absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "renamed shift failed save after retry no-op save should keep all shared handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift failed save after retry no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift failed save after retry no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift failed save after retry no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift failed save after retry no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed shift failed save after retry no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed shift failed save after retry no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "renamed shift failed save after retry no-op output should match the safe retry output");
    check_reopened_renamed_shift_noop_output(
        noop_output, "renamed shift failed save after retry no-op output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "renamed shift failed save after retry second no-op save should keep all shared handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift failed save after retry second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift failed save after retry second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift failed save after retry second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift failed save after retry second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "renamed shift failed save after retry second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "renamed shift failed save after retry second no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift failed save after retry second no-op save should keep source entries unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift failed save after retry second no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift failed save after retry second no-op save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed shift failed save after retry second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "renamed shift failed save after retry second no-op output should match the first no-op output");
    check_reopened_renamed_shift_noop_output(
        second_noop_output,
        "renamed shift failed save after retry second no-op output");

    after_retry.delete_rows(3, 1);
    const std::size_t deleted_memory = after_retry.estimated_memory_usage();
    check(after_retry.has_pending_changes() && sheet.has_pending_changes() &&
            reacquired.has_pending_changes(),
        "renamed shift failed save after retry later delete should dirty all shared handles");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift failed save after retry later delete should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 2,
        "renamed shift failed save after retry later delete should shrink the dirty sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == deleted_memory,
        "renamed shift failed save after retry later delete should report the dirty memory");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "renamed shift failed save after retry later delete should expose one dirty summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" &&
                    summaries[0].planned_name == "RenamedData" &&
                    summaries[0].renamed &&
                    summaries[0].materialized_dirty,
                "renamed shift failed save after retry later delete summary should retain planned state");
            check(summaries[0].materialized_cell_count == 2,
                "renamed shift failed save after retry later delete summary should report sparse count");
            check(summaries[0].estimated_materialized_memory_usage == deleted_memory,
                "renamed shift failed save after retry later delete summary should report memory");
        }
    }
    check(!after_retry.try_cell("A3").has_value() &&
            !sheet.try_cell("A3").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "renamed shift failed save after retry later delete should remove the shifted source row");
    check(after_retry.get_cell("C1").number_value() == 1.0,
        "renamed shift failed save after retry later delete should preserve the shifted number");

    editor.save_as(third_output);
    check(!after_retry.has_pending_changes() && !sheet.has_pending_changes() &&
            !reacquired.has_pending_changes(),
        "renamed shift failed save after retry third save should clean all shared handles");
    check(editor.pending_change_count() == 4,
        "renamed shift failed save after retry third save should record the third materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift failed save after retry third save should clear dirty diagnostics");

    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    const std::string third_workbook_xml = third_entries.at("xl/workbook.xml");
    const std::string third_worksheet_xml = third_entries.at("xl/worksheets/sheet1.xml");
    check_contains(third_workbook_xml, R"(name="RenamedData")",
        "renamed shift failed save after retry third output should keep the planned catalog name");
    check_not_contains(third_workbook_xml, R"(name="Data")",
        "renamed shift failed save after retry third output should omit the source catalog name");
    check_contains(third_worksheet_xml, R"(<dimension ref="A1:C1"/>)",
        "renamed shift failed save after retry third output should shrink after deleting row 3");
    check_contains(third_worksheet_xml, R"(<c r="A1")",
        "renamed shift failed save after retry third output should keep A1");
    check_contains(third_worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "renamed shift failed save after retry third output should keep shifted B1");
    check_not_contains(third_worksheet_xml, R"(r="A3")",
        "renamed shift failed save after retry third output should omit deleted row 3");
    check_not_contains(third_worksheet_xml, R"(r="B1")",
        "renamed shift failed save after retry third output should keep old B1 absent");
    check_not_contains(third_worksheet_xml, R"(r="A2")",
        "renamed shift failed save after retry third output should keep old A2 absent");

    fastxlsx::WorkbookEditor third_reopened = fastxlsx::WorkbookEditor::open(third_output);
    check(third_reopened.has_worksheet("RenamedData") &&
            !third_reopened.has_worksheet("Data"),
        "renamed shift failed save after retry third reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor third_reopened_sheet =
        third_reopened.worksheet("RenamedData");
    check(!third_reopened.has_pending_changes() &&
            !third_reopened_sheet.has_pending_changes(),
        "renamed shift failed save after retry third reopened output should start clean");
    check(third_reopened.pending_change_count() == 0 &&
            third_reopened.pending_materialized_worksheet_names().empty() &&
            third_reopened.pending_materialized_cell_count() == 0 &&
            third_reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift failed save after retry third reopened output should not expose dirty diagnostics");
    check(third_reopened_sheet.cell_count() == 2,
        "renamed shift failed save after retry third reopened output should shrink sparse count");
    check_cell_range_equals(third_reopened_sheet.used_range(), 1, 1, 1, 3,
        "renamed shift failed save after retry third reopened output should expose shrunken bounds");
    check(third_reopened_sheet.get_cell("C1").number_value() == 1.0,
        "renamed shift failed save after retry third reopened output should read shifted B1");
    check(!third_reopened_sheet.try_cell("A3").has_value() &&
            !third_reopened_sheet.try_cell("B1").has_value() &&
            !third_reopened_sheet.try_cell("A2").has_value(),
        "renamed shift failed save after retry third reopened output should keep deleted and old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_option_mismatch_preserves_planned_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-options-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-options-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-options-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-options-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-options-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-options-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();

    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed shift option mismatch first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed shift option mismatch first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift option mismatch first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift option mismatch first save should keep diagnostics clear");

    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("RenamedData", mismatched_options);
    }), "renamed shift option mismatch try_worksheet should reject different options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("RenamedData", mismatched_options);
    }), "renamed shift option mismatch worksheet should reject different options");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed shift option mismatch should keep the old source name unavailable");
    check(!editor.last_edit_error().has_value(),
        "renamed shift option mismatch should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "renamed shift option mismatch should leave the saved planned-name handle clean");
    check(editor.pending_change_count() == 2,
        "renamed shift option mismatch should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift option mismatch should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift option mismatch should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed shift option mismatch should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed shift option mismatch should preserve the planned workbook catalog");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift option mismatch should preserve the saved shifted row");
    check(!sheet.try_cell("A2").has_value(),
        "renamed shift option mismatch should keep old shifted rows absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed shift option mismatch matching reacquire should stay clean");
    check(reacquired.name() == "RenamedData",
        "renamed shift option mismatch matching reacquire should expose the planned name");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift option mismatch matching reacquire should reuse saved shifted state");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "renamed shift option mismatch matching reacquire should keep old row absent");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed shift option mismatch later shift should dirty the shared planned-name session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift option mismatch later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 3,
        "renamed shift option mismatch later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed shift option mismatch later shift should report the shared memory estimate");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "renamed shift option mismatch later shift should expose one renamed dirty summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" &&
                    summaries[0].planned_name == "RenamedData" &&
                    summaries[0].renamed &&
                    summaries[0].materialized_dirty,
                "renamed shift option mismatch later shift summary should retain planned state");
            check(summaries[0].materialized_cell_count == 3,
                "renamed shift option mismatch later shift summary should report sparse count");
            check(summaries[0].estimated_materialized_memory_usage == shifted_memory,
                "renamed shift option mismatch later shift summary should report memory estimate");
        }
    }
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "renamed shift option mismatch later shift should be visible through the older handle");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            !reacquired.try_cell("B1").has_value(),
        "renamed shift option mismatch later shift should retain the row shift and move columns");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift option mismatch second save should clean both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift option mismatch second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift option mismatch second save should clear dirty diagnostics again");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed shift option mismatch first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed shift option mismatch first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "renamed shift option mismatch first output should keep shifted row bounds");
    check_contains(first_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "renamed shift option mismatch first output should keep B1 before later shift");
    check_contains(first_worksheet_xml, R"(<c r="A3")",
        "renamed shift option mismatch first output should contain shifted A2");
    check_not_contains(first_worksheet_xml, R"(r="C1")",
        "renamed shift option mismatch first output should not include later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed shift option mismatch second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed shift option mismatch second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed shift option mismatch second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "renamed shift option mismatch second output should include shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed shift option mismatch second output should keep shifted A2");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed shift option mismatch second output should omit old B1");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed shift option mismatch second output should keep old row coordinate absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift option mismatch no-op save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift option mismatch no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift option mismatch no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift option mismatch no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift option mismatch no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed shift option mismatch no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed shift option mismatch no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "renamed shift option mismatch no-op output should match the second output");
    check_reopened_renamed_shift_noop_output(
        noop_output, "renamed shift option mismatch no-op output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift option mismatch second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift option mismatch second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift option mismatch second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift option mismatch second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift option mismatch second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "renamed shift option mismatch second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "renamed shift option mismatch second no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift option mismatch second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift option mismatch second no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift option mismatch second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed shift option mismatch second no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "renamed shift option mismatch second no-op output should match the first no-op output");
    check_reopened_renamed_shift_noop_output(
        second_noop_output,
        "renamed shift option mismatch second no-op output");

    reacquired.set_cell("D3", fastxlsx::CellValue::text("option-mismatch-post-noop-renamed"));
    const std::size_t post_noop_memory = reacquired.estimated_memory_usage();
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed shift option mismatch post-noop edit should dirty both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift option mismatch post-noop edit should not count dirty state as a saved handoff");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift option mismatch post-noop edit should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed shift option mismatch post-noop edit should add one sparse cell");
    check(editor.estimated_pending_materialized_memory_usage() == post_noop_memory,
        "renamed shift option mismatch post-noop edit should report the dirty memory");
    check(!editor.last_edit_error().has_value(),
        "renamed shift option mismatch post-noop edit should keep diagnostics clear");
    check(sheet.get_cell("D3").text_value() == "option-mismatch-post-noop-renamed" &&
            reacquired.get_cell("D3").text_value() == "option-mismatch-post-noop-renamed",
        "renamed shift option mismatch post-noop edit should be visible through both handles");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "renamed shift option mismatch post-noop edit should preserve shifted source cells");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor,
        sheet,
        reacquired,
        4,
        post_noop_memory,
        "renamed shift option mismatch post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift option mismatch post-noop save should clean both planned-name handles");
    check(editor.pending_change_count() == 4,
        "renamed shift option mismatch post-noop save should record the third materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift option mismatch post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift option mismatch post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift option mismatch post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift option mismatch post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift option mismatch post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift option mismatch post-noop save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed shift option mismatch post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "renamed shift option mismatch post-noop save should leave the second no-op output unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_workbook_xml = post_noop_entries.at("xl/workbook.xml");
    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_workbook_xml, R"(name="RenamedData")",
        "renamed shift option mismatch post-noop output should keep the planned catalog name");
    check_not_contains(post_noop_workbook_xml, R"(name="Data")",
        "renamed shift option mismatch post-noop output should omit the source catalog name");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "renamed shift option mismatch post-noop output should expand bounds to D3");
    check_contains(post_noop_worksheet_xml, R"(<c r="D3")",
        "renamed shift option mismatch post-noop output should write the later D3 cell");
    check_contains(post_noop_worksheet_xml, "option-mismatch-post-noop-renamed",
        "renamed shift option mismatch post-noop output should write the later D3 text");
    check_not_contains(post_noop_worksheet_xml, R"(r="B1")",
        "renamed shift option mismatch post-noop output should keep old B1 absent");
    check_not_contains(post_noop_worksheet_xml, R"(r="A2")",
        "renamed shift option mismatch post-noop output should keep old A2 absent");
    check_reopened_clean_sheet_output(
        post_noop_output, "RenamedData",
        "renamed shift option mismatch post-noop output",
        [](fastxlsx::WorksheetEditor& post_noop_sheet) {
            check(post_noop_sheet.cell_count() == 4,
                "renamed shift option mismatch post-noop output should keep sparse count");
            check_cell_range_equals(post_noop_sheet.used_range(), 1, 1, 3, 4,
                "renamed shift option mismatch post-noop output should expose post-noop bounds");
            check(post_noop_sheet.get_cell("C1").number_value() == 1.0,
                "renamed shift option mismatch post-noop output should read shifted B1");
            check(post_noop_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "renamed shift option mismatch post-noop output should read shifted A2");
            check(post_noop_sheet.get_cell("D3").text_value() == "option-mismatch-post-noop-renamed",
                "renamed shift option mismatch post-noop output should read the later D3 edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                post_noop_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 4 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "option-mismatch-post-noop-renamed",
                "renamed shift option mismatch post-noop row_cells should expose shifted row order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                post_noop_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 3 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "option-mismatch-post-noop-renamed",
                "renamed shift option mismatch post-noop column_cells should expose the later edit");
            check(!post_noop_sheet.try_cell("B1").has_value() &&
                    !post_noop_sheet.try_cell("A2").has_value(),
                "renamed shift option mismatch post-noop output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed shift option mismatch reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed shift option mismatch reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift option mismatch reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 3,
        "renamed shift option mismatch reopened output should keep sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed shift option mismatch reopened output should expose combined bounds");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0,
        "renamed shift option mismatch reopened output should read shifted B1");
    check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift option mismatch reopened output should keep shifted A2");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("A2").has_value(),
        "renamed shift option mismatch reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_missing_query_preserves_planned_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-missing-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-missing-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-missing-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-missing-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-missing-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-missing-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();

    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed shift missing query first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed shift missing query first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift missing query first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift missing query first save should keep diagnostics clear");

    const std::optional<fastxlsx::WorksheetEditor> missing = editor.try_worksheet("Missing");
    check(!missing.has_value(),
        "renamed shift missing query try_worksheet should report a missing sheet");
    const std::optional<fastxlsx::WorksheetEditor> old_source_name =
        editor.try_worksheet("Data");
    check(!old_source_name.has_value(),
        "renamed shift missing query should keep the old source name unavailable");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Missing"); }),
        "renamed shift missing query worksheet should reject a missing sheet");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
        "renamed shift missing query worksheet should reject the old source name");
    check(!editor.last_edit_error().has_value(),
        "renamed shift missing query should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "renamed shift missing query should leave the saved planned-name handle clean");
    check(editor.pending_change_count() == 2,
        "renamed shift missing query should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift missing query should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift missing query should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed shift missing query should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed shift missing query should preserve the planned workbook catalog");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift missing query should preserve the saved shifted row");
    check(!sheet.try_cell("A2").has_value(),
        "renamed shift missing query should keep old shifted rows absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed shift missing query matching reacquire should stay clean");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift missing query matching reacquire should reuse saved shifted state");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "renamed shift missing query matching reacquire should keep old row absent");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed shift missing query later shift should dirty the shared planned-name session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift missing query later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 3,
        "renamed shift missing query later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed shift missing query later shift should report the shared memory estimate");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "renamed shift missing query later shift should be visible through the older handle");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            !reacquired.try_cell("B1").has_value(),
        "renamed shift missing query later shift should retain the row shift and move columns");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift missing query second save should clean both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift missing query second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift missing query second save should clear dirty diagnostics again");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed shift missing query first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed shift missing query first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "renamed shift missing query first output should keep shifted row bounds");
    check_contains(first_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "renamed shift missing query first output should keep B1 before later shift");
    check_contains(first_worksheet_xml, R"(<c r="A3")",
        "renamed shift missing query first output should contain shifted A2");
    check_not_contains(first_worksheet_xml, R"(r="C1")",
        "renamed shift missing query first output should not include later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed shift missing query second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed shift missing query second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed shift missing query second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "renamed shift missing query second output should include shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed shift missing query second output should keep shifted A2");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed shift missing query second output should omit old B1");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed shift missing query second output should keep old row coordinate absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift missing query no-op save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift missing query no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift missing query no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift missing query no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift missing query no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed shift missing query no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed shift missing query no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "renamed shift missing query no-op output should match the second output");
    check_reopened_renamed_shift_noop_output(
        noop_output, "renamed shift missing query no-op output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift missing query second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift missing query second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift missing query second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift missing query second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift missing query second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "renamed shift missing query second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "renamed shift missing query second no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift missing query second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift missing query second no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift missing query second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed shift missing query second no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "renamed shift missing query second no-op output should match the first no-op output");
    check_reopened_renamed_shift_noop_output(
        second_noop_output,
        "renamed shift missing query second no-op output");

    reacquired.set_cell("D3", fastxlsx::CellValue::text("missing-query-post-noop-renamed"));
    const std::size_t post_noop_memory = reacquired.estimated_memory_usage();
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed shift missing query post-noop edit should dirty both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift missing query post-noop edit should not count dirty state as a saved handoff");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift missing query post-noop edit should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed shift missing query post-noop edit should add one sparse cell");
    check(editor.estimated_pending_materialized_memory_usage() == post_noop_memory,
        "renamed shift missing query post-noop edit should report the dirty memory");
    check(!editor.last_edit_error().has_value(),
        "renamed shift missing query post-noop edit should keep diagnostics clear");
    check(sheet.get_cell("D3").text_value() == "missing-query-post-noop-renamed" &&
            reacquired.get_cell("D3").text_value() == "missing-query-post-noop-renamed",
        "renamed shift missing query post-noop edit should be visible through both handles");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "renamed shift missing query post-noop edit should preserve shifted source cells");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor,
        sheet,
        reacquired,
        4,
        post_noop_memory,
        "renamed shift missing query post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift missing query post-noop save should clean both planned-name handles");
    check(editor.pending_change_count() == 4,
        "renamed shift missing query post-noop save should record the third materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift missing query post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift missing query post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift missing query post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift missing query post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift missing query post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift missing query post-noop save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed shift missing query post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "renamed shift missing query post-noop save should leave the second no-op output unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_workbook_xml = post_noop_entries.at("xl/workbook.xml");
    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_workbook_xml, R"(name="RenamedData")",
        "renamed shift missing query post-noop output should keep the planned catalog name");
    check_not_contains(post_noop_workbook_xml, R"(name="Data")",
        "renamed shift missing query post-noop output should omit the source catalog name");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "renamed shift missing query post-noop output should expand bounds to D3");
    check_contains(post_noop_worksheet_xml, R"(<c r="D3")",
        "renamed shift missing query post-noop output should write the later D3 cell");
    check_contains(post_noop_worksheet_xml, "missing-query-post-noop-renamed",
        "renamed shift missing query post-noop output should write the later D3 text");
    check_not_contains(post_noop_worksheet_xml, R"(r="B1")",
        "renamed shift missing query post-noop output should keep old B1 absent");
    check_not_contains(post_noop_worksheet_xml, R"(r="A2")",
        "renamed shift missing query post-noop output should keep old A2 absent");
    check_reopened_clean_sheet_output(
        post_noop_output, "RenamedData",
        "renamed shift missing query post-noop output",
        [](fastxlsx::WorksheetEditor& post_noop_sheet) {
            check(post_noop_sheet.cell_count() == 4,
                "renamed shift missing query post-noop output should keep sparse count");
            check_cell_range_equals(post_noop_sheet.used_range(), 1, 1, 3, 4,
                "renamed shift missing query post-noop output should expose post-noop bounds");
            check(post_noop_sheet.get_cell("C1").number_value() == 1.0,
                "renamed shift missing query post-noop output should read shifted B1");
            check(post_noop_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "renamed shift missing query post-noop output should read shifted A2");
            check(post_noop_sheet.get_cell("D3").text_value() == "missing-query-post-noop-renamed",
                "renamed shift missing query post-noop output should read the later D3 edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                post_noop_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 4 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "missing-query-post-noop-renamed",
                "renamed shift missing query post-noop row_cells should expose shifted row order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                post_noop_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 3 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "missing-query-post-noop-renamed",
                "renamed shift missing query post-noop column_cells should expose the later edit");
            check(!post_noop_sheet.try_cell("B1").has_value() &&
                    !post_noop_sheet.try_cell("A2").has_value(),
                "renamed shift missing query post-noop output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed shift missing query reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed shift missing query reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift missing query reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 3,
        "renamed shift missing query reopened output should keep sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed shift missing query reopened output should expose combined bounds");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0,
        "renamed shift missing query reopened output should read shifted B1");
    check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift missing query reopened output should keep shifted A2");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("A2").has_value(),
        "renamed shift missing query reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_invalid_reads_preserve_planned_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-read-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-read-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-read-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-read-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-read-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-read-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();

    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed shift invalid reads first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed shift invalid reads first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid reads first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid reads first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed shift invalid reads matching reacquire should stay clean before failures");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift invalid reads matching reacquire should reuse saved shifted state");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed shift invalid reads should keep the old source name unavailable");

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "renamed shift invalid reads should reject row-zero try_cell on the original handle");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "renamed shift invalid reads should reject column-zero get_cell on the reacquired handle");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "renamed shift invalid reads should reject lowercase A1 reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("XFE1"); }),
        "renamed shift invalid reads should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "renamed shift invalid reads should reject invalid CellRange reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.sparse_cells("B2:A1"); }),
        "renamed shift invalid reads should reject reversed A1 range reads");
    check(threw_fastxlsx_error([&] {
        const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1048577, 1},
        };
        (void)sheet.sparse_cells(invalid_batch);
    }), "renamed shift invalid reads should reject invalid coordinate batch reads");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(0); }),
        "renamed shift invalid reads should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)reacquired.column_cells(16385); }),
        "renamed shift invalid reads should reject column_cells column overflow");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("D4"); }),
        "renamed shift invalid reads should reject valid but missing get_cell reads");

    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid reads should not update last_edit_error");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid reads should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed shift invalid reads should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid reads should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift invalid reads should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed shift invalid reads should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed shift invalid reads should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed shift invalid reads should preserve planned-name lookup state");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "renamed shift invalid reads should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 2,
        "renamed shift invalid reads should preserve shifted bounds");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift invalid reads should preserve shifted source cells");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "renamed shift invalid reads should keep old row coordinates absent");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed shift invalid reads later shift should dirty the shared planned-name session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift invalid reads later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 3,
        "renamed shift invalid reads later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed shift invalid reads later shift should report the shared memory estimate");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "renamed shift invalid reads later shift should be visible through the older handle");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            !reacquired.try_cell("B1").has_value(),
        "renamed shift invalid reads later shift should retain the row shift and move columns");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid reads second save should clean both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift invalid reads second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid reads second save should clear dirty diagnostics again");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed shift invalid reads first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed shift invalid reads first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "renamed shift invalid reads first output should keep shifted row bounds");
    check_contains(first_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "renamed shift invalid reads first output should keep B1 before later shift");
    check_contains(first_worksheet_xml, R"(<c r="A3")",
        "renamed shift invalid reads first output should contain shifted A2");
    check_not_contains(first_worksheet_xml, R"(r="C1")",
        "renamed shift invalid reads first output should not include later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed shift invalid reads second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed shift invalid reads second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed shift invalid reads second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "renamed shift invalid reads second output should include shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed shift invalid reads second output should keep shifted A2");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed shift invalid reads second output should omit old B1");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed shift invalid reads second output should keep old row coordinate absent");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid reads no-op save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift invalid reads no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid reads no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift invalid reads no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid reads no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed shift invalid reads no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed shift invalid reads no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "renamed shift invalid reads no-op output should match the second output");
    check_reopened_renamed_shift_noop_output(
        noop_output, "renamed shift invalid reads no-op output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid reads second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift invalid reads second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid reads second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift invalid reads second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid reads second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "renamed shift invalid reads second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "renamed shift invalid reads second no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift invalid reads second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift invalid reads second no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift invalid reads second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed shift invalid reads second no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "renamed shift invalid reads second no-op output should match the first no-op output");
    check_reopened_renamed_shift_noop_output(
        second_noop_output,
        "renamed shift invalid reads second no-op output");

    reacquired.set_cell("D3", fastxlsx::CellValue::text("invalid-read-post-noop-renamed"));
    const std::size_t post_noop_memory = reacquired.estimated_memory_usage();
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed shift invalid reads post-noop edit should dirty both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift invalid reads post-noop edit should not count dirty state as a saved handoff");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift invalid reads post-noop edit should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed shift invalid reads post-noop edit should add one sparse cell");
    check(editor.estimated_pending_materialized_memory_usage() == post_noop_memory,
        "renamed shift invalid reads post-noop edit should report the dirty memory");
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid reads post-noop edit should keep diagnostics clear");
    check(sheet.get_cell("D3").text_value() == "invalid-read-post-noop-renamed" &&
            reacquired.get_cell("D3").text_value() == "invalid-read-post-noop-renamed",
        "renamed shift invalid reads post-noop edit should be visible through both handles");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "renamed shift invalid reads post-noop edit should preserve shifted source cells");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor,
        sheet,
        reacquired,
        4,
        post_noop_memory,
        "renamed shift invalid reads post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid reads post-noop save should clean both planned-name handles");
    check(editor.pending_change_count() == 4,
        "renamed shift invalid reads post-noop save should record the third materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid reads post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift invalid reads post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid reads post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift invalid reads post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift invalid reads post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift invalid reads post-noop save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed shift invalid reads post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "renamed shift invalid reads post-noop save should leave the second no-op output unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_workbook_xml = post_noop_entries.at("xl/workbook.xml");
    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_workbook_xml, R"(name="RenamedData")",
        "renamed shift invalid reads post-noop output should keep the planned catalog name");
    check_not_contains(post_noop_workbook_xml, R"(name="Data")",
        "renamed shift invalid reads post-noop output should omit the source catalog name");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "renamed shift invalid reads post-noop output should expand bounds to D3");
    check_contains(post_noop_worksheet_xml, R"(<c r="D3")",
        "renamed shift invalid reads post-noop output should write the later D3 cell");
    check_contains(post_noop_worksheet_xml, "invalid-read-post-noop-renamed",
        "renamed shift invalid reads post-noop output should write the later D3 text");
    check_not_contains(post_noop_worksheet_xml, R"(r="B1")",
        "renamed shift invalid reads post-noop output should keep old B1 absent");
    check_not_contains(post_noop_worksheet_xml, R"(r="A2")",
        "renamed shift invalid reads post-noop output should keep old A2 absent");
    check_reopened_clean_sheet_output(
        post_noop_output, "RenamedData",
        "renamed shift invalid reads post-noop output",
        [](fastxlsx::WorksheetEditor& post_noop_sheet) {
            check(post_noop_sheet.cell_count() == 4,
                "renamed shift invalid reads post-noop output should keep sparse count");
            check_cell_range_equals(post_noop_sheet.used_range(), 1, 1, 3, 4,
                "renamed shift invalid reads post-noop output should expose post-noop bounds");
            check(post_noop_sheet.get_cell("C1").number_value() == 1.0,
                "renamed shift invalid reads post-noop output should read shifted B1");
            check(post_noop_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "renamed shift invalid reads post-noop output should read shifted A2");
            check(post_noop_sheet.get_cell("D3").text_value() == "invalid-read-post-noop-renamed",
                "renamed shift invalid reads post-noop output should read the later D3 edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                post_noop_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 4 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "invalid-read-post-noop-renamed",
                "renamed shift invalid reads post-noop row_cells should expose shifted row order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                post_noop_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 3 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "invalid-read-post-noop-renamed",
                "renamed shift invalid reads post-noop column_cells should expose the later edit");
            check(!post_noop_sheet.try_cell("B1").has_value() &&
                    !post_noop_sheet.try_cell("A2").has_value(),
                "renamed shift invalid reads post-noop output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed shift invalid reads reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed shift invalid reads reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid reads reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 3,
        "renamed shift invalid reads reopened output should keep sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed shift invalid reads reopened output should expose combined bounds");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0,
        "renamed shift invalid reads reopened output should read shifted B1");
    check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift invalid reads reopened output should keep shifted A2");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("A2").has_value(),
        "renamed shift invalid reads reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_invalid_mutations_preserve_planned_session()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-mutation-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-mutation-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-mutation-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-mutation-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-mutation-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-invalid-mutation-post-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();

    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed shift invalid mutations first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed shift invalid mutations first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid mutations first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid mutations first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed shift invalid mutations matching reacquire should stay clean before failures");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift invalid mutations matching reacquire should reuse saved shifted state");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed shift invalid mutations should keep the old source name unavailable");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-renamed-shift-row-zero"));
    }), "renamed shift invalid mutations should reject row-zero set_cell on the original handle");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell(1, 0,
            fastxlsx::CellValue::text("invalid-renamed-shift-column-zero"));
    }), "renamed shift invalid mutations should reject column-zero set_cell on the reacquired handle");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(1048577, 1,
            fastxlsx::CellValue::text("invalid-renamed-shift-row-overflow"));
    }), "renamed shift invalid mutations should reject row-overflow set_cell");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell(1, 16385,
            fastxlsx::CellValue::text("invalid-renamed-shift-column-overflow"));
    }), "renamed shift invalid mutations should reject column-overflow set_cell");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1",
            fastxlsx::CellValue::text("invalid-renamed-shift-lowercase-a1"));
    }), "renamed shift invalid mutations should reject lowercase A1 set_cell");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("XFE1",
            fastxlsx::CellValue::text("invalid-renamed-shift-a1-column-overflow"));
    }), "renamed shift invalid mutations should reject A1 column-overflow set_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell(0, 1); }),
        "renamed shift invalid mutations should reject row-zero erase_cell");
    check(threw_fastxlsx_error([&] { reacquired.erase_cell(1, 16385); }),
        "renamed shift invalid mutations should reject column-overflow erase_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "renamed shift invalid mutations should reject range erase_cell references");
    check(threw_fastxlsx_error([&] { reacquired.erase_cell("a1"); }),
        "renamed shift invalid mutations should reject lowercase A1 erase_cell");

    check(editor.last_edit_error().has_value(),
        "renamed shift invalid mutations should populate last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(), "WorksheetEditor cell reference is invalid",
            "renamed shift invalid mutations should expose the latest invalid reference diagnostic");
    }
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid mutations should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed shift invalid mutations should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid mutations should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift invalid mutations should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed shift invalid mutations should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed shift invalid mutations should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed shift invalid mutations should preserve planned-name lookup state");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "renamed shift invalid mutations should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 3, 2,
        "renamed shift invalid mutations should preserve shifted bounds");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift invalid mutations should preserve shifted source cells");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "renamed shift invalid mutations should keep old row coordinates absent");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid mutations later valid shift should clear diagnostics");
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed shift invalid mutations later shift should dirty the shared planned-name session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift invalid mutations later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 3,
        "renamed shift invalid mutations later shift should report the shared sparse count once");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed shift invalid mutations later shift should report the shared memory estimate");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "renamed shift invalid mutations later shift should be visible through the older handle");
    check(reacquired.get_cell("A3").text_value() == "placeholder-a2" &&
            !reacquired.try_cell("B1").has_value(),
        "renamed shift invalid mutations later shift should retain the row shift and move columns");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid mutations second save should clean both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift invalid mutations second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid mutations second save should clear dirty diagnostics again");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed shift invalid mutations first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed shift invalid mutations first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "renamed shift invalid mutations first output should keep shifted row bounds");
    check_contains(first_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "renamed shift invalid mutations first output should keep B1 before later shift");
    check_contains(first_worksheet_xml, R"(<c r="A3")",
        "renamed shift invalid mutations first output should contain shifted A2");
    check_not_contains(first_worksheet_xml, R"(r="C1")",
        "renamed shift invalid mutations first output should not include later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed shift invalid mutations second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed shift invalid mutations second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed shift invalid mutations second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "renamed shift invalid mutations second output should include shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed shift invalid mutations second output should keep shifted A2");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed shift invalid mutations second output should omit old B1");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed shift invalid mutations second output should keep old row coordinate absent");
    check_not_contains(second_worksheet_xml, "invalid-renamed-shift-",
        "renamed shift invalid mutations second output should not leak rejected payloads");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid mutations no-op save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift invalid mutations no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid mutations no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift invalid mutations no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid mutations no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed shift invalid mutations no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed shift invalid mutations no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "renamed shift invalid mutations no-op output should match the second output");
    check_reopened_renamed_shift_noop_output(
        noop_output, "renamed shift invalid mutations no-op output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid mutations second no-op save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed shift invalid mutations second no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid mutations second no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift invalid mutations second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid mutations second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "renamed shift invalid mutations second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "renamed shift invalid mutations second no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift invalid mutations second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift invalid mutations second no-op save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift invalid mutations second no-op save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed shift invalid mutations second no-op save should leave the first no-op output unchanged");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "renamed shift invalid mutations second no-op output should match the first no-op output");
    check_reopened_renamed_shift_noop_output(
        second_noop_output,
        "renamed shift invalid mutations second no-op output");

    reacquired.set_cell("D3", fastxlsx::CellValue::text("invalid-mutation-post-noop-renamed"));
    const std::size_t post_noop_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid mutations post-noop edit should keep diagnostics clear");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed shift invalid mutations post-noop edit should dirty both planned-name handles");
    check(editor.pending_change_count() == 3,
        "renamed shift invalid mutations post-noop edit should not count dirty state as a saved handoff");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed shift invalid mutations post-noop edit should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed shift invalid mutations post-noop edit should add one sparse cell");
    check(editor.estimated_pending_materialized_memory_usage() == post_noop_memory,
        "renamed shift invalid mutations post-noop edit should report the dirty memory");
    check(sheet.get_cell("D3").text_value() == "invalid-mutation-post-noop-renamed" &&
            reacquired.get_cell("D3").text_value() == "invalid-mutation-post-noop-renamed",
        "renamed shift invalid mutations post-noop edit should be visible through both handles");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "renamed shift invalid mutations post-noop edit should preserve shifted source cells");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor,
        sheet,
        reacquired,
        4,
        post_noop_memory,
        "renamed shift invalid mutations post-noop edit");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed shift invalid mutations post-noop save should clean both planned-name handles");
    check(editor.pending_change_count() == 4,
        "renamed shift invalid mutations post-noop save should record the third materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid mutations post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed shift invalid mutations post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed shift invalid mutations post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed shift invalid mutations post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(first_output) == first_entries,
        "renamed shift invalid mutations post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_output) == second_entries,
        "renamed shift invalid mutations post-noop save should leave the second output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed shift invalid mutations post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "renamed shift invalid mutations post-noop save should leave the second no-op output unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_workbook_xml = post_noop_entries.at("xl/workbook.xml");
    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_workbook_xml, R"(name="RenamedData")",
        "renamed shift invalid mutations post-noop output should keep the planned catalog name");
    check_not_contains(post_noop_workbook_xml, R"(name="Data")",
        "renamed shift invalid mutations post-noop output should omit the source catalog name");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "renamed shift invalid mutations post-noop output should expand bounds to D3");
    check_contains(post_noop_worksheet_xml, R"(<c r="D3")",
        "renamed shift invalid mutations post-noop output should write the later D3 cell");
    check_contains(post_noop_worksheet_xml, "invalid-mutation-post-noop-renamed",
        "renamed shift invalid mutations post-noop output should write the later D3 text");
    check_not_contains(post_noop_worksheet_xml, "invalid-renamed-shift-",
        "renamed shift invalid mutations post-noop output should not leak rejected payloads");
    check_not_contains(post_noop_worksheet_xml, R"(r="B1")",
        "renamed shift invalid mutations post-noop output should keep old B1 absent");
    check_not_contains(post_noop_worksheet_xml, R"(r="A2")",
        "renamed shift invalid mutations post-noop output should keep old A2 absent");
    check_reopened_clean_sheet_output(
        post_noop_output, "RenamedData",
        "renamed shift invalid mutations post-noop output",
        [](fastxlsx::WorksheetEditor& post_noop_sheet) {
            check(post_noop_sheet.cell_count() == 4,
                "renamed shift invalid mutations post-noop output should keep sparse count");
            check_cell_range_equals(post_noop_sheet.used_range(), 1, 1, 3, 4,
                "renamed shift invalid mutations post-noop output should expose post-noop bounds");
            check(post_noop_sheet.get_cell("C1").number_value() == 1.0,
                "renamed shift invalid mutations post-noop output should read shifted B1");
            check(post_noop_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "renamed shift invalid mutations post-noop output should read shifted A2");
            check(post_noop_sheet.get_cell("D3").text_value() == "invalid-mutation-post-noop-renamed",
                "renamed shift invalid mutations post-noop output should read the later D3 edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                post_noop_sheet.row_cells(3);
            check(row_three.size() == 2 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "placeholder-a2" &&
                    row_three[1].reference.row == 3 &&
                    row_three[1].reference.column == 4 &&
                    row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[1].value.text_value() == "invalid-mutation-post-noop-renamed",
                "renamed shift invalid mutations post-noop row_cells should expose shifted row order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                post_noop_sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 3 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_four[0].value.text_value() == "invalid-mutation-post-noop-renamed",
                "renamed shift invalid mutations post-noop column_cells should expose the later edit");
            check(!post_noop_sheet.try_cell("B1").has_value() &&
                    !post_noop_sheet.try_cell("A2").has_value(),
                "renamed shift invalid mutations post-noop output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed shift invalid mutations reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed shift invalid mutations reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed shift invalid mutations reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 3,
        "renamed shift invalid mutations reopened output should keep sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed shift invalid mutations reopened output should expose combined bounds");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0,
        "renamed shift invalid mutations reopened output should read shifted B1");
    check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
        "renamed shift invalid mutations reopened output should keep shifted A2");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("A2").has_value(),
        "renamed shift invalid mutations reopened output should keep old coordinates absent");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_shift_after_rename_reacquire_reuses_planned_session();
        test_public_worksheet_editor_shift_after_rename_failed_save_preserves_planned_session();
        test_public_worksheet_editor_shift_after_rename_option_mismatch_preserves_planned_session();
        test_public_worksheet_editor_shift_after_rename_missing_query_preserves_planned_session();
        test_public_worksheet_editor_shift_after_rename_invalid_reads_preserve_planned_session();
        test_public_worksheet_editor_shift_after_rename_invalid_mutations_preserve_planned_session();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor public-state reacquire renamed check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor public-state reacquire renamed tests passed\n");
    return 0;
}
