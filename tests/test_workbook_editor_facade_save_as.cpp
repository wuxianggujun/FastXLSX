#include "test_workbook_editor_facade_common.hpp"

namespace {

class ScopedWorkbookEditorSaveAsStagedHook {
public:
    explicit ScopedWorkbookEditorSaveAsStagedHook(
        fastxlsx::detail::WorkbookEditorSaveAsStagedHook hook)
    {
        fastxlsx::detail::testing_set_workbook_editor_save_as_staged_hook(hook);
    }

    ~ScopedWorkbookEditorSaveAsStagedHook()
    {
        fastxlsx::detail::testing_set_workbook_editor_save_as_staged_hook(nullptr);
    }

    ScopedWorkbookEditorSaveAsStagedHook(
        const ScopedWorkbookEditorSaveAsStagedHook&) = delete;
    ScopedWorkbookEditorSaveAsStagedHook& operator=(
        const ScopedWorkbookEditorSaveAsStagedHook&) = delete;
};

void throw_after_workbook_editor_materialized_stage()
{
    throw fastxlsx::FastXlsxError("injected save_as failure after materialized staging");
}

} // namespace

void test_save_as_over_source_throws()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-overwrite-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as over the source package should throw FastXlsxError");
}

void test_noop_save_as_preserves_source_package_entries()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_document_properties(
            "fastxlsx-workbook-editor-noop-save-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-noop-save-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check_workbook_editor_public_clean_state(
        editor, "fresh WorkbookEditor before no-op save_as");

    editor.save_as(output);

    check_workbook_editor_public_clean_state(editor, "no-op save_as");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as should preserve decompressed source package entries");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data"),
        "no-op save_as output should keep the Data sheet");
    check(reopened.has_worksheet("Untouched"),
        "no-op save_as output should keep the Untouched sheet");
}

void test_noop_save_as_preserves_failed_edit_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-noop-save-after-error-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-error-copy.xlsx");
    const std::filesystem::path edited_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-error-edited.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(13.0)}});
    }), "missing-sheet edit should fail before no-op save_as");

    const std::optional<std::string> last_error_before_save = editor.last_edit_error();
    check(last_error_before_save.has_value(),
        "failed edit before no-op save_as should record last_edit_error");
    check_workbook_editor_public_no_pending_state(
        editor, "failed edit before no-op save_as");

    editor.save_as(noop_output);

    check(editor.last_edit_error() == last_error_before_save,
        "no-op save_as should preserve a prior failed-edit diagnostic");
    check_workbook_editor_public_no_pending_state(
        editor, "no-op save_as after a failed edit");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after a failed edit should preserve source entries");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after error")}});
    check(!editor.last_edit_error().has_value(),
        "successful edit after no-op save_as should clear the failed-edit diagnostic");
    editor.save_as(edited_output);

    const auto edited_entries = fastxlsx::test::read_zip_entries(edited_output);
    check_contains(edited_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" t="inlineStr"><is><t>after error</t></is></c>)",
        "later edit after no-op save_as should still write replacement data");
}

void test_noop_save_as_preserves_failed_rename_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-noop-save-after-rename-error-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-rename-error-copy.xlsx");
    const std::filesystem::path renamed_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-rename-error-renamed.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "invalid rename should fail before no-op save_as");

    const std::optional<std::string> last_error_before_save = editor.last_edit_error();
    check(last_error_before_save.has_value(),
        "failed rename before no-op save_as should record last_edit_error");
    if (last_error_before_save.has_value()) {
        check_contains(*last_error_before_save, "Bad/Name",
            "failed rename diagnostic should name the rejected sheet");
    }
    check_workbook_editor_public_no_pending_state(
        editor, "failed rename before no-op save_as");

    editor.save_as(noop_output);

    check(editor.last_edit_error() == last_error_before_save,
        "no-op save_as should preserve a prior failed-rename diagnostic");
    check_workbook_editor_public_no_pending_state(
        editor, "no-op save_as after a failed rename");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after a failed rename should preserve source entries");

    editor.rename_sheet("Data", "CleanName");
    check(!editor.last_edit_error().has_value(),
        "successful rename after no-op save_as should clear the failed-rename diagnostic");
    editor.save_as(renamed_output);

    const auto renamed_entries = fastxlsx::test::read_zip_entries(renamed_output);
    check_contains(renamed_entries.at("xl/workbook.xml"), R"(name="CleanName")",
        "later rename after no-op save_as should still update workbook catalog");
}

void test_noop_save_as_keeps_editor_usable_for_later_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-noop-then-edit-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-noop-then-edit-copy.xlsx");
    const std::filesystem::path edited_output =
        artifact("fastxlsx-workbook-editor-noop-then-edit-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.save_as(noop_output);
    check_workbook_editor_public_clean_state(
        editor, "no-op save_as before later edits");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("after noop"),
            fastxlsx::CellValue::number(42.0)}});
    editor.rename_sheet("Data", "AfterNoop");

    check(editor.has_pending_changes(),
        "editor should remain usable for edits after no-op save_as");
    check(editor.pending_change_count() == 2,
        "replacement plus rename after no-op save_as should be queued");
    check(editor.pending_replacement_cell_count() == 2,
        "replacement after no-op save_as should expose pending cells");
    check(editor.has_worksheet("AfterNoop"),
        "planned catalog should expose rename after no-op save_as");
    check(editor.has_source_worksheet("Data"),
        "source catalog should remain available after no-op save_as");

    editor.save_as(edited_output);

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "later edits should not mutate the prior no-op save_as output");

    const auto edited_entries = fastxlsx::test::read_zip_entries(edited_output);
    check_contains(edited_entries.at("xl/workbook.xml"), R"(name="AfterNoop")",
        "save_as after no-op save_as should write the later queued rename");
    check_contains(edited_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" t="inlineStr"><is><t>after noop</t></is></c>)",
        "save_as after no-op save_as should write the later queued text cell");
    check_contains(edited_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1"><v>42</v></c>)",
        "save_as after no-op save_as should write the later queued number cell");
}

void test_failed_save_as_preserves_public_facade_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-save-failure-state-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-save-failure-state-output.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-save-failure-missing-parent") / "output.xlsx";
    const std::filesystem::path file_parent =
        artifact("fastxlsx-workbook-editor-save-failure-file-parent.bin");
    const std::filesystem::path file_parent_output = file_parent / "output.xlsx";
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-save-failure-directory-output");
    write_binary_file(file_parent, "not a directory");
    std::filesystem::create_directories(directory_output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(11.0)}});
    editor.rename_sheet("Data", "RenamedData");

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(99.0)}});
    }), "old source name replacement should fail after queued rename");
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
        workbook_editor_public_save_state_snapshot(editor);
    check(save_state_before_save.last_edit_error.has_value(),
        "pre-save failed edit should leave a public last_edit_error");
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_save =
        editor.worksheet_catalog();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_save =
        editor.pending_worksheet_edits();

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as over the source package should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path{}); }),
        "save_as with an empty output path should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "save_as with a missing parent directory should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(file_parent_output); }),
        "save_as with a non-directory parent should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "save_as to an existing directory should fail without committing output");

    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_save, "failed save_as");

    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_after_save =
        editor.worksheet_catalog();
    check(workbook_editor_catalog_entries_equal(catalog_after_save, catalog_before_save),
        "failed save_as should preserve worksheet catalog");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_after_save =
        editor.pending_worksheet_edits();
    check(workbook_editor_edit_summaries_equal(
              summaries_after_save, summaries_before_save),
        "failed save_as should preserve pending worksheet edit summaries");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "safe save_as after failed save should keep the queued rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>11</v></c>)",
        "safe save_as after failed save should keep the queued sheetData replacement");

    const std::filesystem::path clean_error_output =
        artifact("fastxlsx-workbook-editor-save-failure-clean-error-output.xlsx");
    fastxlsx::WorkbookEditor clean_error_editor = fastxlsx::WorkbookEditor::open(source);
    clean_error_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(12.0)}});
    check(!clean_error_editor.last_edit_error().has_value(),
        "successful edit before save_as failure should leave last_edit_error empty");

    check(threw_fastxlsx_error([&] {
        clean_error_editor.save_as(std::filesystem::path{});
    }), "save_as failure should throw even when no prior edit failure exists");
    check(!clean_error_editor.last_edit_error().has_value(),
        "failed save_as should not create last_edit_error when none existed");

    clean_error_editor.save_as(clean_error_output);
    const auto clean_error_entries = fastxlsx::test::read_zip_entries(clean_error_output);
    check_contains(clean_error_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>12</v></c>)",
        "safe save_as after clean-error failed save should keep the queued replacement");
}

void test_failed_save_as_after_materialized_stage_preserves_dirty_state()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-materialized-stage-save-failure-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-materialized-stage-save-failure-output.xlsx");
    std::error_code remove_error;
    std::filesystem::remove(output, remove_error);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor worksheet = editor.worksheet("Data");
    worksheet.set_cell(1, 1, fastxlsx::CellValue::text("before-staged-save-failure"));

    check(worksheet.has_pending_changes(),
        "materialized edit should be dirty before injected save failure");
    check(editor.pending_change_count() == 0,
        "dirty materialized edit should not be handed off before save");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "dirty materialized edit should contribute one unsaved change before save");
    const std::vector<std::string> pending_names_before_save =
        editor.pending_materialized_worksheet_names();
    const std::size_t pending_cells_before_save =
        editor.pending_materialized_cell_count();
    const std::size_t pending_memory_before_save =
        editor.estimated_pending_materialized_memory_usage();

    {
        ScopedWorkbookEditorSaveAsStagedHook hook(
            throw_after_workbook_editor_materialized_stage);
        check(threw_fastxlsx_error([&] { editor.save_as(output); }),
            "injected failure after materialized staging should fail save_as");
    }

    check(!std::filesystem::exists(output),
        "injected pre-write save failure should not create the output package");
    check(worksheet.has_pending_changes(),
        "failed save after staging should keep the materialized handle dirty");
    check(editor.pending_change_count() == 0,
        "failed save after staging should not commit a public handoff count");
    check(editor.pending_materialized_worksheet_names() == pending_names_before_save,
        "failed save after staging should preserve dirty worksheet names");
    check(editor.pending_materialized_cell_count() == pending_cells_before_save,
        "failed save after staging should preserve dirty cell diagnostics");
    check(editor.estimated_pending_materialized_memory_usage() == pending_memory_before_save,
        "failed save after staging should preserve dirty memory diagnostics");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "failed save after staging should preserve the unsaved watermark");
    check(!editor.last_edit_error().has_value(),
        "failed save after staging should not create last_edit_error");

    worksheet.set_cell(1, 1, fastxlsx::CellValue::text("after-staged-save-failure"));
    editor.save_as(output);

    check(!worksheet.has_pending_changes(),
        "successful retry should commit and clear the materialized dirty state");
    check(editor.pending_change_count() == 1,
        "successful retry should commit exactly one materialized handoff");
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "successful retry should advance the unsaved watermark");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "after-staged-save-failure",
        "successful retry should write the latest materialized value");
    check_not_contains(worksheet_xml, "before-staged-save-failure",
        "successful retry should replace the stale staged value");

    worksheet.set_cell(2, 1, fastxlsx::CellValue::text("stale-existing-output-failure"));
    check(worksheet.has_pending_changes(),
        "existing-output staged failure setup should dirty the materialized handle");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "existing-output staged failure setup should create one unsaved change");

    {
        ScopedWorkbookEditorSaveAsStagedHook hook(
            throw_after_workbook_editor_materialized_stage);
        check(threw_fastxlsx_error([&] { editor.save_as(output); }),
            "injected failure after materialized staging should fail over existing output");
    }

    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "existing-output staged failure should leave the previous output package unchanged");
    check(worksheet.has_pending_changes(),
        "existing-output failed save after staging should keep the materialized handle dirty");
    check(editor.pending_change_count() == 1,
        "existing-output failed save after staging should not commit another handoff");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "existing-output failed save after staging should preserve the unsaved watermark");
    check(!editor.last_edit_error().has_value(),
        "existing-output failed save after staging should not create last_edit_error");

    worksheet.set_cell(2, 1, fastxlsx::CellValue::text("fresh-existing-output-retry"));
    editor.save_as(output);

    check(!worksheet.has_pending_changes(),
        "existing-output successful retry should clear the materialized handle");
    check(editor.pending_change_count() == 2,
        "existing-output successful retry should commit the second materialized handoff");
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "existing-output successful retry should advance the saved watermark");
    const auto final_output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string final_worksheet_xml =
        final_output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(final_worksheet_xml, "after-staged-save-failure",
        "existing-output successful retry should keep the first retried value");
    check_contains(final_worksheet_xml, "fresh-existing-output-retry",
        "existing-output successful retry should write the latest follow-up value");
    check_not_contains(final_worksheet_xml, "stale-existing-output-failure",
        "existing-output successful retry should replace the stale follow-up value");
    check(final_output_entries != output_entries,
        "existing-output successful retry should update the output package after the failed attempt");
}

void test_failed_save_as_after_multi_sheet_materialized_stage_preserves_all_dirty_state()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-materialized-stage-multi-save-failure-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-materialized-stage-multi-save-failure-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-materialized-stage-multi-save-failure-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-materialized-stage-multi-save-failure-post-noop-output.xlsx");
    std::error_code remove_error;
    std::filesystem::remove(output, remove_error);
    std::filesystem::remove(noop_output, remove_error);
    std::filesystem::remove(post_noop_output, remove_error);

    const auto source_entries_before = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
    data.set_cell(1, 1, fastxlsx::CellValue::text("before-multi-staged-save-failure"));
    untouched.set_cell(1, 2, fastxlsx::CellValue::text("untouched-multi-staged-save-failure"));

    check(data.has_pending_changes() && untouched.has_pending_changes(),
        "multi-sheet staged failure setup should dirty both materialized handles");
    check(editor.pending_change_count() == 0,
        "multi-sheet staged failure setup should not hand off materialized sessions before save");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 2,
        "multi-sheet staged failure setup should count both dirty sessions as unsaved");
    const std::vector<std::string> pending_names_before_save =
        editor.pending_materialized_worksheet_names();
    const std::size_t pending_cells_before_save =
        editor.pending_materialized_cell_count();
    const std::size_t pending_memory_before_save =
        editor.estimated_pending_materialized_memory_usage();

    {
        ScopedWorkbookEditorSaveAsStagedHook hook(
            throw_after_workbook_editor_materialized_stage);
        check(threw_fastxlsx_error([&] { editor.save_as(output); }),
            "injected failure after multi-sheet materialized staging should fail save_as");
    }

    check(!std::filesystem::exists(output),
        "injected multi-sheet pre-write save failure should not create the output package");
    check(data.has_pending_changes() && untouched.has_pending_changes(),
        "multi-sheet failed save after staging should keep both materialized handles dirty");
    check(editor.pending_change_count() == 0,
        "multi-sheet failed save after staging should not commit public handoffs");
    check(editor.pending_materialized_worksheet_names() == pending_names_before_save,
        "multi-sheet failed save after staging should preserve dirty worksheet names");
    check(editor.pending_materialized_cell_count() == pending_cells_before_save,
        "multi-sheet failed save after staging should preserve dirty cell diagnostics");
    check(editor.estimated_pending_materialized_memory_usage() == pending_memory_before_save,
        "multi-sheet failed save after staging should preserve dirty memory diagnostics");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 2,
        "multi-sheet failed save after staging should preserve the unsaved watermark");
    check(!editor.last_edit_error().has_value(),
        "multi-sheet failed save after staging should not create last_edit_error");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before,
        "multi-sheet failed save after staging should leave source bytes unchanged");

    data.set_cell(1, 1, fastxlsx::CellValue::text("after-multi-staged-save-failure"));
    editor.save_as(output);

    check(!data.has_pending_changes() && !untouched.has_pending_changes(),
        "multi-sheet successful retry should clear both materialized handles");
    check(editor.pending_change_count() == 2,
        "multi-sheet successful retry should commit both materialized handoffs");
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "multi-sheet successful retry should advance the unsaved watermark");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_xml = output_entries.at("xl/worksheets/sheet2.xml");
    check_contains(data_xml, "after-multi-staged-save-failure",
        "multi-sheet successful retry should write the latest Data value");
    check_not_contains(data_xml, "before-multi-staged-save-failure",
        "multi-sheet successful retry should replace stale staged Data value");
    check_contains(untouched_xml, "untouched-multi-staged-save-failure",
        "multi-sheet successful retry should keep the second dirty sheet value");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before,
        "multi-sheet successful retry should leave source bytes unchanged");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_data = reopened.worksheet("Data");
    fastxlsx::WorksheetEditor reopened_untouched = reopened.worksheet("Untouched");
    const fastxlsx::CellValue reopened_data_a1 = reopened_data.get_cell("A1");
    const fastxlsx::CellValue reopened_untouched_b1 = reopened_untouched.get_cell("B1");
    check(reopened_data_a1.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_a1.text_value() == "after-multi-staged-save-failure",
        "multi-sheet staged failure retry reopened output should expose latest Data value");
    check(reopened_untouched_b1.kind() == fastxlsx::CellValueKind::Text &&
            reopened_untouched_b1.text_value() == "untouched-multi-staged-save-failure",
        "multi-sheet staged failure retry reopened output should expose Untouched value");

    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!data.has_pending_changes() && !untouched.has_pending_changes(),
        "multi-sheet staged failure retry no-op save should keep handles clean");
    check(editor.pending_change_count() == 2,
        "multi-sheet staged failure retry no-op save should not add another handoff");
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "multi-sheet staged failure retry no-op save should keep the saved watermark");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "multi-sheet staged failure retry no-op save should keep materialized diagnostics clean");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "multi-sheet staged failure retry no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "multi-sheet staged failure retry no-op output should match retry output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before,
        "multi-sheet staged failure retry no-op save should leave source bytes unchanged");

    data.set_cell(3, 3, fastxlsx::CellValue::text("post-noop-staged-save-failure"));
    check(data.has_pending_changes() && !untouched.has_pending_changes(),
        "multi-sheet staged failure retry post-noop edit should dirty only Data");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "multi-sheet staged failure retry post-noop edit should expose only Data dirty name");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "multi-sheet staged failure retry post-noop edit should create one unsaved change");

    editor.save_as(post_noop_output);
    check(!data.has_pending_changes() && !untouched.has_pending_changes(),
        "multi-sheet staged failure retry post-noop save should clean both handles");
    check(editor.pending_change_count() == 3,
        "multi-sheet staged failure retry post-noop save should add one materialized handoff");
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "multi-sheet staged failure retry post-noop save should advance the saved watermark");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_data_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    const std::string post_noop_untouched_xml =
        post_noop_entries.at("xl/worksheets/sheet2.xml");
    check_contains(post_noop_data_xml, "after-multi-staged-save-failure",
        "multi-sheet staged failure retry post-noop output should keep retry Data value");
    check_contains(post_noop_data_xml, "post-noop-staged-save-failure",
        "multi-sheet staged failure retry post-noop output should include the follow-up Data edit");
    check_not_contains(post_noop_data_xml, "before-multi-staged-save-failure",
        "multi-sheet staged failure retry post-noop output should not revive stale Data value");
    check_contains(post_noop_untouched_xml, "untouched-multi-staged-save-failure",
        "multi-sheet staged failure retry post-noop output should preserve Untouched value");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "multi-sheet staged failure retry post-noop save should leave noop output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before,
        "multi-sheet staged failure retry post-noop save should leave source bytes unchanged");
}

void test_successful_save_as_preserves_public_facade_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-success-save-state-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-success-save-state-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-success-save-state-second.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-success-save-state-third.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(21.0)}});
    editor.rename_sheet("Data", "SavedData");
    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(99.0)}});
    }), "old source name replacement should fail after queued rename before save");

    const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
        workbook_editor_public_save_state_snapshot(editor);
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_save =
        editor.worksheet_catalog();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_save =
        editor.pending_worksheet_edits();
    check(save_state_before_save.last_edit_error.has_value(),
        "pre-save failed edit should leave last_edit_error for successful save_as state test");

    editor.save_as(first_output);

    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_save, "successful save_as");

    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_after_save =
        editor.worksheet_catalog();
    check(workbook_editor_catalog_entries_equal(catalog_after_save, catalog_before_save),
        "successful save_as should preserve worksheet catalog");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_after_save =
        editor.pending_worksheet_edits();
    check(workbook_editor_edit_summaries_equal(
              summaries_after_save, summaries_before_save),
        "successful save_as should preserve pending worksheet edit summaries");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="SavedData")",
        "successful save_as should write the queued rename");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>21</v></c>)",
        "successful save_as should write the queued replacement");

    editor.save_as(second_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(second_entries == first_entries,
        "second save_as without new edits should reuse the preserved pending state");

    editor.replace_sheet_data("SavedData", {{fastxlsx::CellValue::number(31.0)}});
    check(editor.pending_change_count() == save_state_before_save.pending_change_count + 1,
        "follow-up edit after successful save_as should add another pending change");
    check(!editor.last_edit_error().has_value(),
        "follow-up successful edit after save_as should clear the prior edit error");

    editor.save_as(third_output);
    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    check_contains(third_entries.at("xl/workbook.xml"), R"(name="SavedData")",
        "follow-up save_as should keep the queued rename");
    check_contains(third_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>31</v></c>)",
        "follow-up save_as should write the later replacement");
    check_not_contains(third_entries.at("xl/worksheets/sheet1.xml"), R"(<v>21</v>)",
        "follow-up save_as should drop the prior replacement payload");

    const auto first_entries_after_follow_up =
        fastxlsx::test::read_zip_entries(first_output);
    check(first_entries_after_follow_up == first_entries,
        "follow-up edits should not mutate the earlier successful save_as output");
}

void test_unsaved_change_watermark_tracks_successful_saves()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-unsaved-watermark-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-unsaved-watermark-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-unsaved-watermark-second-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(!editor.has_unsaved_changes(),
        "newly opened editor should not report unsaved changes");
    check(editor.unsaved_change_count() == 0,
        "newly opened editor should have zero unsaved change count");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(31.0)}});
    check(editor.has_unsaved_changes(),
        "successful Patch edit should report unsaved changes");
    check(editor.unsaved_change_count() == 1,
        "one successful Patch edit should increment unsaved change count");

    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path {}); }),
        "failed save_as should preserve unsaved watermark");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "failed save_as should not reset unsaved change state");

    editor.save_as(output);
    check(!editor.has_unsaved_changes(),
        "successful save_as should clear unsaved change state");
    check(editor.unsaved_change_count() == 0,
        "successful save_as should reset unsaved change count");
    check(editor.has_pending_changes(),
        "successful save_as should retain staged Patch diagnostics");
    check(editor.pending_change_count() == 1,
        "successful save_as should retain staged Patch edit count");

    editor.rename_sheet("Data", "RenamedData");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 1,
        "edit after save_as should create a new unsaved watermark delta");
    editor.save_as(second_output);
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "second successful save_as should advance the watermark again");
}
void test_unsaved_change_watermark_moves_with_editor_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-unsaved-move-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-unsaved-move-output.xlsx");

    fastxlsx::WorkbookEditor source_editor = fastxlsx::WorkbookEditor::open(source);
    source_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(41.0)}});

    fastxlsx::WorkbookEditor moved_editor(std::move(source_editor));
    check(!source_editor.has_unsaved_changes() && source_editor.unsaved_change_count() == 0,
        "moved-from editor should report no unsaved state");
    check(moved_editor.has_unsaved_changes() && moved_editor.unsaved_change_count() == 1,
        "move construction should preserve unsaved watermark state");

    fastxlsx::WorkbookEditor assigned_editor = fastxlsx::WorkbookEditor::open(source);
    assigned_editor = std::move(moved_editor);
    check(!moved_editor.has_unsaved_changes() && moved_editor.unsaved_change_count() == 0,
        "move-assigned-from editor should report no unsaved state");
    check(assigned_editor.has_unsaved_changes() && assigned_editor.unsaved_change_count() == 1,
        "move assignment should preserve unsaved watermark state");

    assigned_editor.save_as(output);
    check(!assigned_editor.has_unsaved_changes()
            && assigned_editor.unsaved_change_count() == 0,
        "successful save after move assignment should advance the moved watermark");
}
void test_empty_rows_emit_empty_sheet_data()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-empty-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-empty-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<sheetData></sheetData>",
        "empty rows should emit an empty sheetData");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "empty replacement should drop old placeholder data");
}

void test_text_uses_inline_strings_and_preserves_shared_strings()
{
    // Build a shared-string source so we can prove the table is preserved, not
    // migrated, when the replacement uses inline strings.
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-shared-source.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("shared-placeholder")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "shared-string source should emit a sharedStrings part");
    const std::string shared_strings_before =
        source_entries.at("xl/sharedStrings.xml");

    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-shared-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("inline-text")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(t="inlineStr")",
        "replacement text should be written as an inline string");
    check_contains(worksheet_xml, "inline-text",
        "replacement text should appear in the worksheet");

    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "existing sharedStrings should be preserved, not migrated or pruned");
}

void test_calc_metadata_requests_recalculation_without_inventing_calcchain()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-calc-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-calc-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/calcChain.xml") == source_entries.end(),
        "streaming writer source should not carry a calcChain");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::formula("SUM(A1:A1)")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "data edit should request workbook recalculation on load");

    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "editor should not invent a calcChain when the source has none");
}

} // namespace

int main()
{
    try {
        test_save_as_over_source_throws();
        test_noop_save_as_preserves_source_package_entries();
        test_noop_save_as_preserves_failed_edit_diagnostic();
        test_noop_save_as_preserves_failed_rename_diagnostic();
        test_noop_save_as_keeps_editor_usable_for_later_edits();
        test_failed_save_as_preserves_public_facade_state();
        test_failed_save_as_after_materialized_stage_preserves_dirty_state();
        test_failed_save_as_after_multi_sheet_materialized_stage_preserves_all_dirty_state();
        test_successful_save_as_preserves_public_facade_state();
        test_unsaved_change_watermark_tracks_successful_saves();
        test_unsaved_change_watermark_moves_with_editor_state();
        test_empty_rows_emit_empty_sheet_data();
        test_text_uses_inline_strings_and_preserves_shared_strings();
        test_calc_metadata_requests_recalculation_without_inventing_calcchain();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor facade save-as check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor facade save-as tests passed\n");
    return 0;
}
