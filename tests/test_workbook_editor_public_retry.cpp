#include "test_workbook_editor_public_retry_common.hpp"

void test_public_worksheet_editor_rename_back_failed_mutation_preserves_clean_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-mutation-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-mutation-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-mutation-noop.xlsx");

    const auto source_entries_before = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientFailure");
    editor.rename_sheet("TransientFailure", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes(),
        "rename-back materialized session should start clean");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("rename-back-invalid"));
    }), "invalid A1 mutation after rename-back should throw");

    const std::optional<std::string> failed_mutation_error =
        editor.last_edit_error();
    check(failed_mutation_error.has_value(),
        "failed rename-back materialized mutation should set last_edit_error");
    check(!sheet.has_pending_changes(),
        "failed mutation after rename-back should preserve clean dirty state");
    check(editor.pending_change_count() == 2,
        "failed mutation after rename-back should not count a materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed mutation after rename-back should not add dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "failed mutation after rename-back should not add dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed mutation after rename-back should not add dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "failed mutation after rename-back should preserve empty current summaries");
    check(editor.has_worksheet("Data") && !editor.has_worksheet("TransientFailure"),
        "failed mutation after rename-back should preserve restored planned catalog");

    sheet.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-recovered-after-failure"));
    check(!editor.last_edit_error().has_value(),
        "successful recovery mutation after rename-back should clear last_edit_error");
    check(sheet.has_pending_changes(),
        "successful recovery mutation after rename-back should dirty the session");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "recovered rename-back diagnostics should use the restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "recovered mutation after rename-back should create one current summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "recovered rename-back summary should use restored source/planned names");
            check(!summary.renamed,
                "recovered rename-back summary should not be marked renamed");
            check(summary.materialized_dirty,
                "recovered rename-back summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "recovered rename-back summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "save_as after recovered rename-back mutation should clear dirty state");
    check(editor.pending_change_count() == 3,
        "save_as after recovered rename-back mutation should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after recovered rename-back mutation should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after recovered rename-back mutation should clear dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "save_as after recovered rename-back mutation should clear dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after recovered rename-back mutation should clear current summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "save_as after recovered rename-back mutation");
    check(!editor.last_edit_error().has_value(),
        "save_as after recovered rename-back mutation should keep last_edit_error clear");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "recovered rename-back output should keep the restored source name");
    check_not_contains(output_entries.at("xl/workbook.xml"), "TransientFailure",
        "recovered rename-back output should not leak the transient planned name");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "rename-back-invalid",
        "failed rename-back mutation payload should not leak into output");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-recovered-after-failure",
        "recovered rename-back mutation should persist after save_as");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before,
        "recovered rename-back mutation save_as should leave the source package unchanged");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save after recovered rename-back mutation should keep the handle clean");
    check(editor.pending_change_count() == 3,
        "no-op save after recovered rename-back mutation should not count another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "no-op save after recovered rename-back mutation should keep dirty names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "no-op save after recovered rename-back mutation should keep dirty cell count empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "no-op save after recovered rename-back mutation should keep dirty memory empty");
    check(editor.pending_worksheet_edits().empty(),
        "no-op save after recovered rename-back mutation should keep summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "no-op save after recovered rename-back mutation");
    check(!editor.last_edit_error().has_value(),
        "no-op save after recovered rename-back mutation should keep last_edit_error clear");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "no-op save after recovered rename-back mutation should preserve output package XML");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before,
        "no-op save after recovered rename-back mutation should still leave the source package unchanged");
}

void test_public_worksheet_editor_rename_back_failed_save_as_preserves_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-noop.xlsx");

    const auto source_entries_before = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientSave");
    editor.rename_sheet("TransientSave", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-dirty-before-failed-save"));

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();

    check(sheet.has_pending_changes(),
        "rename-back failed-save setup should leave the borrowed session dirty");
    check(editor.pending_change_count() == 2,
        "rename-back failed-save setup should count only the two rename calls before save");
    check(!editor.last_edit_error().has_value(),
        "rename-back failed-save setup should start without last_edit_error");

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as over source after rename-back dirty edit should fail before auto-flush");

    const auto source_entries_after = fastxlsx::test::read_zip_entries(source);
    check(source_entries_after == source_entries_before,
        "rejected source-overwrite save_as after rename-back should not mutate source package bytes");
    check(sheet.has_pending_changes(),
        "rejected save_as after rename-back should keep the borrowed session dirty");
    check(editor.pending_change_count() == 2,
        "rejected save_as after rename-back should not count a materialized handoff");
    check(!editor.last_edit_error().has_value(),
        "rejected save_as after rename-back should not create last_edit_error");
    check(editor.has_worksheet("Data") && !editor.has_worksheet("TransientSave"),
        "rejected save_as after rename-back should preserve restored planned catalog");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "rejected save_as after rename-back should preserve restored dirty name");
    }
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "rejected save_as after rename-back should preserve materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "rejected save_as after rename-back should preserve materialized memory");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "rejected save_as after rename-back should preserve one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "rejected save_as rename-back summary should use restored names");
            check(!summary.renamed,
                "rejected save_as rename-back summary should not remain marked renamed");
            check(!summary.sheet_data_replaced,
                "rejected save_as rename-back summary should not invent replacement diagnostics");
            check(summary.materialized_dirty,
                "rejected save_as rename-back summary should keep materialized dirty flag");
            check(summary.materialized_cell_count == dirty_cell_count,
                "rejected save_as rename-back summary should preserve cell count");
            check(summary.estimated_materialized_memory_usage == dirty_memory_usage,
                "rejected save_as rename-back summary should preserve memory estimate");
        }
    }

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "safe save_as after rename-back rejection should flush dirty state");
    check(editor.pending_change_count() == 3,
        "safe save_as after rename-back rejection should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "safe save_as after rename-back rejection should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "safe save_as after rename-back rejection should clear dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "safe save_as after rename-back rejection should clear dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "safe save_as after rename-back rejection should clear current summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "safe save_as after rename-back rejection");
    check(!editor.last_edit_error().has_value(),
        "safe save_as after rename-back rejection should keep last_edit_error clear");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "rename-back failed-save recovery output should keep the restored source name");
    check_not_contains(output_entries.at("xl/workbook.xml"), "TransientSave",
        "rename-back failed-save recovery output should not leak the transient planned name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-dirty-before-failed-save",
        "rename-back failed-save recovery output should include the materialized edit");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "rename-back failed-save recovery output should replace the old source value");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before,
        "rename-back failed-save recovery should leave the source package unchanged");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save after rename-back failed-save recovery should keep the handle clean");
    check(editor.pending_change_count() == 3,
        "no-op save after rename-back failed-save recovery should not count another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "no-op save after rename-back failed-save recovery should keep dirty names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "no-op save after rename-back failed-save recovery should keep dirty cell count empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "no-op save after rename-back failed-save recovery should keep dirty memory empty");
    check(editor.pending_worksheet_edits().empty(),
        "no-op save after rename-back failed-save recovery should keep summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "no-op save after rename-back failed-save recovery");
    check(!editor.last_edit_error().has_value(),
        "no-op save after rename-back failed-save recovery should keep last_edit_error clear");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "no-op save after rename-back failed-save recovery should preserve output package XML");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before,
        "no-op save after rename-back failed-save recovery should still leave the source package unchanged");
}

void test_public_worksheet_editor_rename_back_failed_save_as_reacquire_reuses_saved_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-reacquire-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-reacquire-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-reacquire-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientReacquire");
    editor.rename_sheet("TransientReacquire", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-reacquire-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before rename-back reacquire setup flushes");
    editor.save_as(first_output);

    check(!sheet.has_pending_changes(),
        "safe save_as after rename-back failed save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "safe save_as after rename-back failed save should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "safe save_as before reacquire should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "safe save_as before reacquire should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "safe save_as before reacquire should clear dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "safe save_as before reacquire should clear rename-back dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "safe save_as before reacquire");
    check(!editor.last_edit_error().has_value(),
        "failed save_as plus safe save_as should not create last_edit_error");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!reacquired.has_pending_changes(),
        "matching reacquire after rename-back failed-save recovery should start clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-reacquire-first",
        "matching reacquire after rename-back failed-save recovery should reuse saved materialized state");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean reacquire after rename-back failed-save recovery should not dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "clean reacquire after rename-back failed-save recovery should not dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "clean reacquire after rename-back failed-save recovery should not dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "clean reacquire after rename-back failed-save recovery should keep summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "clean reacquire after rename-back failed-save recovery");
    check(!editor.last_edit_error().has_value(),
        "clean reacquire after rename-back failed-save recovery should keep last_edit_error clear");

    reacquired.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-reacquire-second"));
    check(sheet.has_pending_changes(),
        "post-reacquire mutation should dirty the shared materialized session visible to older handle");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "post-reacquire rename-back dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "post-reacquire rename-back mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "post-reacquire rename-back summary should use restored names");
            check(!summary.renamed,
                "post-reacquire rename-back summary should not be marked renamed");
            check(summary.materialized_dirty,
                "post-reacquire rename-back summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "post-reacquire rename-back summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean both borrowed handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear rename-back reacquire summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "second safe save_as");
    check(!editor.last_edit_error().has_value(),
        "second safe save_as should keep last_edit_error clear");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value that reacquire must not reload");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-first",
        "source package should not contain the saved materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first rename-back reacquire output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientReacquire",
        "first rename-back reacquire output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-first",
        "first output should include the saved materialized value");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-second",
        "first output should not include the later post-reacquire edit");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second rename-back reacquire output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientReacquire",
        "second rename-back reacquire output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-first",
        "second output should preserve the saved materialized value after reacquire");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-reacquire-second",
        "second output should include the post-reacquire mutation");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = workbook_editor_shard_from_args(argc, argv);
        std::printf("fastxlsx.workbook_editor shard: %.*s\n",
            static_cast<int>(shard.size()), shard.data());

        if (should_run_workbook_editor_shard(shard, "public-retry")) {
            test_public_worksheet_editor_rename_back_failed_mutation_preserves_clean_diagnostics();
            test_public_worksheet_editor_rename_back_failed_save_as_preserves_dirty_state();
            test_public_worksheet_editor_rename_back_failed_save_as_reacquire_reuses_saved_state();
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor tests passed\n");
    return 0;
}
