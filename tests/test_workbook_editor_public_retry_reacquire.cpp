#include "test_workbook_editor_public_retry_common.hpp"

void test_public_worksheet_editor_rename_back_failed_save_as_option_mismatch_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;
    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 9;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientOptionMismatch");
    editor.rename_sheet("TransientOptionMismatch", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-option-mismatch-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before option-mismatch recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before option mismatch should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-option-mismatch-first",
        "matching reacquire before option mismatch should reuse saved materialized state");
    check(editor.pending_change_count() == 3,
        "safe save before option mismatch should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "safe save before option mismatch should leave dirty names empty");
    check(editor.pending_materialized_cell_count() == 0,
        "safe save before option mismatch should leave dirty cell count empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "safe save before option mismatch should leave dirty memory empty");
    check(editor.pending_worksheet_edits().empty(),
        "safe save before option mismatch should leave dirty summaries empty");
    check(!editor.last_edit_error().has_value(),
        "rename-back failed-save recovery should not create last_edit_error");

    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("Data", mismatched_options);
    }), "try_worksheet should reject mismatched options after failed-save recovery");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data", mismatched_options);
    }), "worksheet should reject mismatched options after failed-save recovery");

    check(!editor.last_edit_error().has_value(),
        "post-recovery option mismatch should not update last_edit_error");
    check(editor.pending_change_count() == 3,
        "post-recovery option mismatch should not queue another public edit");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "post-recovery option mismatch should keep existing handles clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-recovery option mismatch should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "post-recovery option mismatch should keep dirty cell count clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-recovery option mismatch should keep dirty memory clear");
    check(editor.pending_worksheet_edits().empty(),
        "post-recovery option mismatch should keep summaries empty");
    check(editor.has_worksheet("Data") &&
            !editor.has_worksheet("TransientOptionMismatch"),
        "post-recovery option mismatch should preserve the restored planned catalog name");

    const fastxlsx::CellValue preserved_value = reacquired.get_cell(1, 1);
    check(preserved_value.kind() == fastxlsx::CellValueKind::Text &&
            preserved_value.text_value() == "rename-back-option-mismatch-first",
        "post-recovery option mismatch should preserve the saved materialized value");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after post-recovery option mismatch should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-option-mismatch-first",
        "matching reacquire after option mismatch should still use the saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-option-mismatch-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "valid post-mismatch mutation should dirty older shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-mismatch dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-mismatch mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-mismatch summary should use restored names");
            check(!summary.renamed,
                "valid post-mismatch summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-mismatch summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-mismatch summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all option-mismatch recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear dirty names after option mismatch");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear dirty cell count after option mismatch");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear dirty memory after option mismatch");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after option mismatch");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first option-mismatch recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientOptionMismatch",
        "first option-mismatch recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-first",
        "first output should contain the saved value before option mismatch");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-second",
        "first output should not contain the later post-mismatch mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second option-mismatch recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientOptionMismatch",
        "second option-mismatch recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-first",
        "second output should preserve the saved value after option mismatch");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-second",
        "second output should include the valid post-mismatch mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after option mismatch");
}

void test_public_worksheet_editor_rename_back_failed_save_as_missing_try_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMissingTry");
    editor.rename_sheet("TransientMissingTry", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-missing-try-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before missing-try recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before missing try_worksheet should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-missing-try-first",
        "matching reacquire before missing try_worksheet should reuse saved materialized state");

    const std::optional<fastxlsx::WorksheetEditor> missing_transient =
        editor.try_worksheet("TransientMissingTry", options);
    check(!missing_transient.has_value(),
        "try_worksheet should return empty for the old transient planned name");
    const std::optional<fastxlsx::WorksheetEditor> missing =
        editor.try_worksheet("Missing", options);
    check(!missing.has_value(),
        "try_worksheet should return empty for a missing name after failed-save recovery");

    check(!editor.last_edit_error().has_value(),
        "post-recovery missing try_worksheet should not update last_edit_error");
    check(editor.pending_change_count() == 3,
        "post-recovery missing try_worksheet should not queue another public edit");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "post-recovery missing try_worksheet should keep existing handles clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-recovery missing try_worksheet should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "post-recovery missing try_worksheet should keep dirty cell count clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-recovery missing try_worksheet should keep dirty memory clear");
    check(editor.pending_worksheet_edits().empty(),
        "post-recovery missing try_worksheet should keep summaries empty");
    check(editor.has_worksheet("Data") && !editor.has_worksheet("TransientMissingTry"),
        "post-recovery missing try_worksheet should preserve the restored planned catalog name");

    const fastxlsx::CellValue preserved_value = reacquired.get_cell(1, 1);
    check(preserved_value.kind() == fastxlsx::CellValueKind::Text &&
            preserved_value.text_value() == "rename-back-missing-try-first",
        "post-recovery missing try_worksheet should preserve the saved materialized value");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after missing try_worksheet should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-missing-try-first",
        "matching reacquire after missing try_worksheet should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-missing-try-second"));
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-missing-try dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-missing-try mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-missing-try summary should use restored names");
            check(!summary.renamed,
                "valid post-missing-try summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-missing-try summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-missing-try summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all missing-try recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after missing try_worksheet");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after missing try_worksheet");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first missing-try recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMissingTry",
        "first missing-try recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-first",
        "first output should contain the saved value before missing try_worksheet");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-second",
        "first output should not contain the later post-missing-try mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second missing-try recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientMissingTry",
        "second missing-try recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-first",
        "second output should preserve the saved value after missing try_worksheet");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-second",
        "second output should include the valid post-missing-try mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after missing try_worksheet");
}

void test_public_worksheet_editor_rename_back_failed_save_as_missing_worksheet_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMissingWorksheet");
    editor.rename_sheet("TransientMissingWorksheet", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-missing-worksheet-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before missing-worksheet recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before missing worksheet should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-missing-worksheet-first",
        "matching reacquire before missing worksheet should reuse saved materialized state");

    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("TransientMissingWorksheet", options);
    }), "worksheet should throw for the old transient planned name");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Missing", options);
    }), "worksheet should throw for a missing name after failed-save recovery");

    check(!editor.last_edit_error().has_value(),
        "post-recovery missing worksheet should not update last_edit_error");
    check(editor.pending_change_count() == 3,
        "post-recovery missing worksheet should not queue another public edit");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "post-recovery missing worksheet should keep existing handles clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-recovery missing worksheet should not dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "post-recovery missing worksheet should keep dirty cell count clear");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-recovery missing worksheet should keep dirty memory clear");
    check(editor.pending_worksheet_edits().empty(),
        "post-recovery missing worksheet should keep summaries empty");
    check(editor.has_worksheet("Data") &&
            !editor.has_worksheet("TransientMissingWorksheet"),
        "post-recovery missing worksheet should preserve the restored planned catalog name");

    const fastxlsx::CellValue preserved_value = reacquired.get_cell(1, 1);
    check(preserved_value.kind() == fastxlsx::CellValueKind::Text &&
            preserved_value.text_value() == "rename-back-missing-worksheet-first",
        "post-recovery missing worksheet should preserve the saved materialized value");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after missing worksheet should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-missing-worksheet-first",
        "matching reacquire after missing worksheet should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-missing-worksheet-second"));
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-missing-worksheet dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-missing-worksheet mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-missing-worksheet summary should use restored names");
            check(!summary.renamed,
                "valid post-missing-worksheet summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-missing-worksheet summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-missing-worksheet summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all missing-worksheet recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after missing worksheet");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after missing worksheet");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first missing-worksheet recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMissingWorksheet",
        "first missing-worksheet recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-first",
        "first output should contain the saved value before missing worksheet");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-second",
        "first output should not contain the later post-missing-worksheet mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second missing-worksheet recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientMissingWorksheet",
        "second missing-worksheet recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-first",
        "second output should preserve the saved value after missing worksheet");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-second",
        "second output should include the valid post-missing-worksheet mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after missing worksheet");
}

void test_public_worksheet_editor_rename_back_failed_save_as_catalog_queries_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientCatalogQuery");
    editor.rename_sheet("TransientCatalogQuery", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-catalog-query-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before catalog-query recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before catalog queries should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-catalog-query-first",
        "matching reacquire before catalog queries should reuse saved materialized state");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    {
        const std::vector<std::string> names = editor.worksheet_names();
        check(names.size() == 2 && names[0] == "Data" && names[1] == "Untouched",
            "planned catalog query after recovery should report restored names");
    }
    check(editor.has_worksheet("Data"),
        "planned catalog query after recovery should find restored Data");
    check(editor.has_worksheet("Untouched"),
        "planned catalog query after recovery should find untouched sheet");
    check(!editor.has_worksheet("TransientCatalogQuery"),
        "planned catalog query after recovery should not revive transient name");
    check(!editor.has_worksheet("Missing"),
        "planned catalog query after recovery should reject absent names");

    {
        const std::vector<std::string> source_names = editor.source_worksheet_names();
        check(source_names.size() == 2 && source_names[0] == "Data" &&
                source_names[1] == "Untouched",
            "source catalog query after recovery should report original names");
    }
    check(editor.has_source_worksheet("Data"),
        "source catalog query after recovery should find source Data");
    check(editor.has_source_worksheet("Untouched"),
        "source catalog query after recovery should find source untouched sheet");
    check(!editor.has_source_worksheet("TransientCatalogQuery"),
        "source catalog query after recovery should not expose transient planned name");
    check(!editor.has_source_worksheet("Missing"),
        "source catalog query after recovery should reject absent source names");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-catalog-query-first",
        "TransientCatalogQuery",
        "post-recovery catalog queries",
        3);

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after catalog queries should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-catalog-query-first",
        "matching reacquire after catalog queries should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-catalog-query-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-catalog-query mutation should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-catalog-query dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-catalog-query mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-catalog-query summary should use restored names");
            check(!summary.renamed,
                "valid post-catalog-query summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-catalog-query summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-catalog-query summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all catalog-query recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after catalog queries");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after catalog queries");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after catalog queries");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-first",
        "source package should not contain the saved catalog-query materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first catalog-query recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientCatalogQuery",
        "first catalog-query recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-first",
        "first output should contain the saved value before catalog queries");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-second",
        "first output should not contain the later post-catalog-query mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second catalog-query recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientCatalogQuery",
        "second catalog-query recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-first",
        "second output should preserve the saved value after catalog queries");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-second",
        "second output should include the valid post-catalog-query mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after catalog queries");
}

void test_public_worksheet_editor_rename_back_failed_save_as_diagnostics_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-second.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientDiagnostics");
    editor.rename_sheet("TransientDiagnostics", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-diagnostics-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before diagnostic-query recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before diagnostics should keep both handles clean");
    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-diagnostics-first",
        "matching reacquire before diagnostics should reuse saved materialized state");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    check(editor.has_pending_changes(),
        "post-save recovery should still expose prior public edits as pending facade state");
    check(editor.pending_change_count() == 3,
        "post-save recovery should count rename, rename-back, and materialized handoff");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "post-save recovery should not invent replacement diagnostics");
    check(editor.pending_materialized_worksheet_names().empty(),
        "post-save recovery should start with clean materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "post-save recovery should start with clean materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "post-save recovery should start with clean materialized memory");
    check(!editor.has_pending_replacement("Data") &&
            !editor.has_pending_replacement("TransientDiagnostics") &&
            !editor.has_pending_replacement("Missing"),
        "post-save recovery should not report replacement payloads");
    check(editor.pending_worksheet_edits().empty(),
        "post-save recovery should not expose dirty materialized summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "post-save recovery catalog diagnostic should keep source workbook sheet count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "Data" && !catalog[0].renamed,
                "post-save recovery catalog diagnostic should show restored Data mapping");
            check(catalog[1].source_name == "Untouched" &&
                    catalog[1].planned_name == "Untouched" && !catalog[1].renamed,
                "post-save recovery catalog diagnostic should preserve untouched mapping");
        }
    }
    check(!editor.last_edit_error().has_value(),
        "post-save recovery should start diagnostics with no last_edit_error");

    check_public_inspection_preserves_last_edit_error(editor, editor.last_edit_error());
    (void)editor.has_pending_replacement("TransientDiagnostics");
    (void)editor.has_pending_replacement("Missing");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-diagnostics-first",
        "TransientDiagnostics",
        "post-recovery diagnostic queries",
        3);

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after diagnostics should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-diagnostics-first",
        "matching reacquire after diagnostics should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-diagnostics-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-diagnostic-query mutation should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-diagnostic-query dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-diagnostic-query mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-diagnostic-query summary should use restored names");
            check(!summary.renamed,
                "valid post-diagnostic-query summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-diagnostic-query summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-diagnostic-query summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all diagnostic-query recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after diagnostics");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after diagnostics");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-first",
        "source package should not contain the saved diagnostic-query materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first diagnostic-query recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientDiagnostics",
        "first diagnostic-query recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-first",
        "first output should contain the saved value before diagnostics");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-second",
        "first output should not contain the later post-diagnostic-query mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second diagnostic-query recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientDiagnostics",
        "second diagnostic-query recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-first",
        "second output should preserve the saved value after diagnostics");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-second",
        "second output should include the valid post-diagnostic-query mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after diagnostics");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = workbook_editor_shard_from_args(argc, argv);
        std::printf("fastxlsx.workbook_editor shard: %.*s\n",
            static_cast<int>(shard.size()), shard.data());

        if (should_run_workbook_editor_shard(shard, "public-retry-reacquire")) {
            test_public_worksheet_editor_rename_back_failed_save_as_option_mismatch_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_missing_try_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_missing_worksheet_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_catalog_queries_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_diagnostics_preserve_reacquired_state();
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
