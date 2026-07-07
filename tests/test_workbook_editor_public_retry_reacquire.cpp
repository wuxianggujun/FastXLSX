#include "test_workbook_editor_public_retry_common.hpp"

void check_retry_cell_range_equals(
    const std::optional<fastxlsx::CellRange>& range,
    std::uint32_t first_row,
    std::uint32_t first_column,
    std::uint32_t last_row,
    std::uint32_t last_column,
    std::string_view message)
{
    check(range.has_value() && range->first_row == first_row &&
            range->first_column == first_column && range->last_row == last_row &&
            range->last_column == last_column,
        message);
}

void check_retry_reopened_clean_state(
    const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::string_view scenario)
{
    const std::string prefix(scenario);

    check(!sheet.has_pending_changes(),
        prefix + " should materialize the worksheet handle as clean public state");
    check_workbook_editor_public_clean_state(editor, prefix);
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should not expose dirty materialized diagnostics");
}

void check_retry_reacquire_safe_save_clean_state(
    const fastxlsx::WorkbookEditor& editor,
    std::size_t expected_pending_change_count,
    std::string_view scenario)
{
    const std::string prefix(scenario);

    check(editor.pending_change_count() == expected_pending_change_count,
        prefix + " should count the expected materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        prefix + " should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        prefix + " should clear dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should clear dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should clear dirty materialized summaries");
    check_workbook_editor_no_replacement_diagnostics(editor, prefix);
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear");
}

void test_public_worksheet_editor_rename_back_failed_save_as_option_mismatch_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-option-mismatch-noop.xlsx");

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
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "second safe option-mismatch save");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after option mismatch");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-first",
        "source package should not contain the first materialized option-mismatch edit");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-option-mismatch-second",
        "source package should not contain the later option-mismatch edit");

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

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "option-mismatch no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == 4,
        "option-mismatch no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "option-mismatch no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "option-mismatch no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "option-mismatch no-op save");
    check(!editor.last_edit_error().has_value(),
        "option-mismatch no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "option-mismatch no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "option-mismatch no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "option-mismatch no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "option-mismatch no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_editor =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor noop_sheet =
        noop_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        noop_editor, noop_sheet, "option-mismatch no-op reopened output");
    check(noop_sheet.cell_count() == 4,
        "option-mismatch no-op reopened output should preserve sparse cell count");
    check_retry_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 2,
        "option-mismatch no-op reopened output should preserve sparse used range");
    const fastxlsx::CellValue noop_a1 = noop_sheet.get_cell("A1");
    const fastxlsx::CellValue noop_b1 = noop_sheet.get_cell("B1");
    const fastxlsx::CellValue noop_a2 = noop_sheet.get_cell("A2");
    const fastxlsx::CellValue noop_b2 = noop_sheet.get_cell("B2");
    check(noop_a1.kind() == fastxlsx::CellValueKind::Text &&
            noop_a1.text_value() == "rename-back-option-mismatch-first",
        "option-mismatch no-op reopened output should read the saved A1 text");
    check(noop_b1.kind() == fastxlsx::CellValueKind::Number &&
            noop_b1.number_value() == 1.0,
        "option-mismatch no-op reopened output should preserve source-backed B1");
    check(noop_a2.kind() == fastxlsx::CellValueKind::Text &&
            noop_a2.text_value() == "placeholder-a2",
        "option-mismatch no-op reopened output should preserve source-backed A2");
    check(noop_b2.kind() == fastxlsx::CellValueKind::Text &&
            noop_b2.text_value() == "rename-back-option-mismatch-second",
        "option-mismatch no-op reopened output should read the post-mismatch edit");
}

void test_public_worksheet_editor_rename_back_failed_save_as_missing_try_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-try-noop.xlsx");

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
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "second safe missing-try save");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after missing try_worksheet");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-first",
        "source package should not contain the first materialized missing-try edit");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-try-second",
        "source package should not contain the later missing-try edit");

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

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "missing-try no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == 4,
        "missing-try no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "missing-try no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "missing-try no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "missing-try no-op save");
    check(!editor.last_edit_error().has_value(),
        "missing-try no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "missing-try no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "missing-try no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "missing-try no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "missing-try no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_editor =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor noop_sheet =
        noop_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        noop_editor, noop_sheet, "missing-try no-op reopened output");
    check(noop_sheet.cell_count() == 4,
        "missing-try no-op reopened output should preserve sparse cell count");
    check_retry_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 2,
        "missing-try no-op reopened output should preserve sparse used range");
    const fastxlsx::CellValue noop_a1 = noop_sheet.get_cell("A1");
    const fastxlsx::CellValue noop_b1 = noop_sheet.get_cell("B1");
    const fastxlsx::CellValue noop_a2 = noop_sheet.get_cell("A2");
    const fastxlsx::CellValue noop_b2 = noop_sheet.get_cell("B2");
    check(noop_a1.kind() == fastxlsx::CellValueKind::Text &&
            noop_a1.text_value() == "rename-back-missing-try-first",
        "missing-try no-op reopened output should read the saved A1 text");
    check(noop_b1.kind() == fastxlsx::CellValueKind::Number &&
            noop_b1.number_value() == 1.0,
        "missing-try no-op reopened output should preserve source-backed B1");
    check(noop_a2.kind() == fastxlsx::CellValueKind::Text &&
            noop_a2.text_value() == "placeholder-a2",
        "missing-try no-op reopened output should preserve source-backed A2");
    check(noop_b2.kind() == fastxlsx::CellValueKind::Text &&
            noop_b2.text_value() == "rename-back-missing-try-second",
        "missing-try no-op reopened output should read the post-missing-try edit");
}

void test_public_worksheet_editor_rename_back_failed_save_as_missing_worksheet_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-worksheet-noop.xlsx");

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
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "second safe missing-worksheet save");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after missing worksheet");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-first",
        "source package should not contain the first materialized missing-worksheet edit");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-worksheet-second",
        "source package should not contain the later missing-worksheet edit");

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

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "missing-worksheet no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == 4,
        "missing-worksheet no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "missing-worksheet no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "missing-worksheet no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "missing-worksheet no-op save");
    check(!editor.last_edit_error().has_value(),
        "missing-worksheet no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "missing-worksheet no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "missing-worksheet no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "missing-worksheet no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "missing-worksheet no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_editor =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor noop_sheet =
        noop_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        noop_editor, noop_sheet, "missing-worksheet no-op reopened output");
    check(noop_sheet.cell_count() == 4,
        "missing-worksheet no-op reopened output should preserve sparse cell count");
    check_retry_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 2,
        "missing-worksheet no-op reopened output should preserve sparse used range");
    const fastxlsx::CellValue noop_a1 = noop_sheet.get_cell("A1");
    const fastxlsx::CellValue noop_b1 = noop_sheet.get_cell("B1");
    const fastxlsx::CellValue noop_a2 = noop_sheet.get_cell("A2");
    const fastxlsx::CellValue noop_b2 = noop_sheet.get_cell("B2");
    check(noop_a1.kind() == fastxlsx::CellValueKind::Text &&
            noop_a1.text_value() == "rename-back-missing-worksheet-first",
        "missing-worksheet no-op reopened output should read the saved A1 text");
    check(noop_b1.kind() == fastxlsx::CellValueKind::Number &&
            noop_b1.number_value() == 1.0,
        "missing-worksheet no-op reopened output should preserve source-backed B1");
    check(noop_a2.kind() == fastxlsx::CellValueKind::Text &&
            noop_a2.text_value() == "placeholder-a2",
        "missing-worksheet no-op reopened output should preserve source-backed A2");
    check(noop_b2.kind() == fastxlsx::CellValueKind::Text &&
            noop_b2.text_value() == "rename-back-missing-worksheet-second",
        "missing-worksheet no-op reopened output should read the post-missing-worksheet edit");
}

void test_public_worksheet_editor_rename_back_failed_save_as_catalog_queries_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-catalog-query-noop.xlsx");

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
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "second safe catalog-query save");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after catalog queries");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-first",
        "source package should not contain the saved catalog-query materialized value");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-catalog-query-second",
        "source package should not contain the later catalog-query materialized value");

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

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "catalog-query no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == 4,
        "catalog-query no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "catalog-query no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "catalog-query no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "catalog-query no-op save");
    check(!editor.last_edit_error().has_value(),
        "catalog-query no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "catalog-query no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "catalog-query no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "catalog-query no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "catalog-query no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_editor =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor noop_sheet =
        noop_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        noop_editor, noop_sheet, "catalog-query no-op reopened output");
    check(noop_sheet.cell_count() == 4,
        "catalog-query no-op reopened output should preserve sparse cell count");
    check_retry_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 2,
        "catalog-query no-op reopened output should preserve sparse used range");
    const fastxlsx::CellValue noop_a1 = noop_sheet.get_cell("A1");
    const fastxlsx::CellValue noop_b1 = noop_sheet.get_cell("B1");
    const fastxlsx::CellValue noop_a2 = noop_sheet.get_cell("A2");
    const fastxlsx::CellValue noop_b2 = noop_sheet.get_cell("B2");
    check(noop_a1.kind() == fastxlsx::CellValueKind::Text &&
            noop_a1.text_value() == "rename-back-catalog-query-first",
        "catalog-query no-op reopened output should read the saved A1 text");
    check(noop_b1.kind() == fastxlsx::CellValueKind::Number &&
            noop_b1.number_value() == 1.0,
        "catalog-query no-op reopened output should preserve source-backed B1");
    check(noop_a2.kind() == fastxlsx::CellValueKind::Text &&
            noop_a2.text_value() == "placeholder-a2",
        "catalog-query no-op reopened output should preserve source-backed A2");
    check(noop_b2.kind() == fastxlsx::CellValueKind::Text &&
            noop_b2.text_value() == "rename-back-catalog-query-second",
        "catalog-query no-op reopened output should read the post-catalog-query edit");
}

void test_public_worksheet_editor_rename_back_failed_save_as_diagnostics_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-diagnostics-noop.xlsx");

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
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "second safe diagnostic-query save");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after diagnostics");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-first",
        "source package should not contain the saved diagnostic-query materialized value");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-diagnostics-second",
        "source package should not contain the later diagnostic-query materialized value");

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

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "diagnostic-query no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == 4,
        "diagnostic-query no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "diagnostic-query no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "diagnostic-query no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "diagnostic-query no-op save");
    check(!editor.last_edit_error().has_value(),
        "diagnostic-query no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "diagnostic-query no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "diagnostic-query no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "diagnostic-query no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "diagnostic-query no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_editor =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor noop_sheet =
        noop_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        noop_editor, noop_sheet, "diagnostic-query no-op reopened output");
    check(noop_sheet.cell_count() == 4,
        "diagnostic-query no-op reopened output should preserve sparse cell count");
    check_retry_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 2,
        "diagnostic-query no-op reopened output should preserve sparse used range");
    const fastxlsx::CellValue noop_a1 = noop_sheet.get_cell("A1");
    const fastxlsx::CellValue noop_b1 = noop_sheet.get_cell("B1");
    const fastxlsx::CellValue noop_a2 = noop_sheet.get_cell("A2");
    const fastxlsx::CellValue noop_b2 = noop_sheet.get_cell("B2");
    check(noop_a1.kind() == fastxlsx::CellValueKind::Text &&
            noop_a1.text_value() == "rename-back-diagnostics-first",
        "diagnostic-query no-op reopened output should read the saved A1 text");
    check(noop_b1.kind() == fastxlsx::CellValueKind::Number &&
            noop_b1.number_value() == 1.0,
        "diagnostic-query no-op reopened output should preserve source-backed B1");
    check(noop_a2.kind() == fastxlsx::CellValueKind::Text &&
            noop_a2.text_value() == "placeholder-a2",
        "diagnostic-query no-op reopened output should preserve source-backed A2");
    check(noop_b2.kind() == fastxlsx::CellValueKind::Text &&
            noop_b2.text_value() == "rename-back-diagnostics-second",
        "diagnostic-query no-op reopened output should read the post-diagnostic-query edit");
}

void test_public_worksheet_editor_rename_back_failed_save_as_shift_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-shift-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-shift-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-shift-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-shift-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientShift");
    editor.rename_sheet("TransientShift", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("rename-back-shift-first"));
    sheet.set_cell(2, 3, fastxlsx::CellValue::formula("A1+B2"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before shift recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before shift should keep both handles clean");
    const fastxlsx::CellValue saved_text = reacquired.get_cell(1, 1);
    check(saved_text.kind() == fastxlsx::CellValueKind::Text &&
            saved_text.text_value() == "rename-back-shift-first",
        "matching reacquire before shift should reuse saved text state");
    const fastxlsx::CellValue saved_formula = reacquired.get_cell(2, 3);
    check(saved_formula.kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula.text_value() == "A1+B2",
        "matching reacquire before shift should reuse saved formula state");
    check(!editor.last_edit_error().has_value(),
        "rename-back shift recovery should not create last_edit_error");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire before row shift should remain clean");
    matching.insert_rows(2, 1);

    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "post-reacquire row shift should dirty all shared handles");
    check(matching.get_cell("A1").text_value() == "rename-back-shift-first",
        "post-reacquire row shift should preserve rows before the insertion point");
    check(!matching.try_cell("C2").has_value(),
        "post-reacquire row shift should remove the old formula coordinate");
    const fastxlsx::CellValue shifted_formula = matching.get_cell("C3");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula.text_value() == "A2+B3",
        "post-reacquire row shift should translate the moved formula text");
    check_retry_cell_range_equals(matching.used_range(), 1, 1, 3, 3,
        "post-reacquire row shift should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_three =
        matching.row_cells(3);
    check(shifted_row_three.size() == 2 &&
            shifted_row_three[0].reference.row == 3 &&
            shifted_row_three[0].reference.column == 1 &&
            shifted_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_three[0].value.text_value() == "placeholder-a2" &&
            shifted_row_three[1].reference.row == 3 &&
            shifted_row_three[1].reference.column == 3 &&
            shifted_row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_three[1].value.text_value() == "A2+B3",
        "post-reacquire row shift row_cells should expose shifted source and formula cells");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_three =
        matching.column_cells(3);
    check(shifted_column_three.size() == 1 &&
            shifted_column_three[0].reference.row == 3 &&
            shifted_column_three[0].reference.column == 3 &&
            shifted_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_three[0].value.text_value() == "A2+B3",
        "post-reacquire row shift column_cells should expose the shifted formula cell");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "post-reacquire row shift dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "post-reacquire row shift should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "post-reacquire row shift summary should use restored names");
            check(!summary.renamed,
                "post-reacquire row shift summary should not be marked renamed");
            check(summary.materialized_dirty,
                "post-reacquire row shift summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "post-reacquire row shift summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all shift recovery handles");
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "second safe row-shift save");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first shift recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientShift",
        "first shift recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-shift-first",
        "first output should contain the saved text before the row shift");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C2"><f>A1+B2</f></c>)",
        "first output should contain the saved formula before the row shift");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C3"><f>A2+B3</f></c>)",
        "first output should not contain the later shifted formula");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second shift recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientShift",
        "second shift recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-shift-first",
        "second output should preserve the saved text after the row shift");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C3"><f>A2+B3</f></c>)",
        "second output should persist the shifted translated formula");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C2"><f>A1+B2</f></c>)",
        "second output should not keep the old formula coordinate");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "TransientShift",
        "second shift worksheet output should not leak the transient planned name");

    fastxlsx::WorkbookEditor reopened_editor =
        fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        reopened_editor, reopened_sheet, "reopened row-shift output");
    check(reopened_sheet.get_cell("A1").text_value() == "rename-back-shift-first",
        "reopened row-shift output should read back preserved row-one text");
    check(!reopened_sheet.try_cell("C2").has_value(),
        "reopened row-shift output should not read back the old formula coordinate");
    const fastxlsx::CellValue reopened_formula = reopened_sheet.get_cell("C3");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == "A2+B3",
        "reopened row-shift output should read back translated formula");
    check_retry_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "reopened row-shift output should read back shifted sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
        reopened_sheet.row_cells(3);
    check(reopened_row_three.size() == 2 &&
            reopened_row_three[0].reference.row == 3 &&
            reopened_row_three[0].reference.column == 1 &&
            reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_row_three[0].value.text_value() == "placeholder-a2" &&
            reopened_row_three[1].reference.row == 3 &&
            reopened_row_three[1].reference.column == 3 &&
            reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_row_three[1].value.text_value() == "A2+B3",
        "reopened row-shift row_cells should expose shifted sparse cells");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
        reopened_sheet.column_cells(3);
    check(reopened_column_three.size() == 1 &&
            reopened_column_three[0].reference.row == 3 &&
            reopened_column_three[0].reference.column == 3 &&
            reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_column_three[0].value.text_value() == "A2+B3",
        "reopened row-shift column_cells should expose shifted formula");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "row-shift no-op save should keep all recovery handles clean");
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "row-shift no-op save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "row-shift no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "row-shift no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "row-shift no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row-shift no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_editor =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor noop_sheet =
        noop_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        noop_editor, noop_sheet, "row-shift no-op reopened output");
    check(noop_sheet.get_cell("A1").text_value() == "rename-back-shift-first",
        "row-shift no-op reopened output should read back preserved row-one text");
    check(!noop_sheet.try_cell("C2").has_value(),
        "row-shift no-op reopened output should not read back the old formula coordinate");
    const fastxlsx::CellValue noop_formula = noop_sheet.get_cell("C3");
    check(noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula.text_value() == "A2+B3",
        "row-shift no-op reopened output should read back translated formula");
    check_retry_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 3,
        "row-shift no-op reopened output should read back shifted sparse used range");
}

void test_public_worksheet_editor_rename_back_failed_save_as_column_shift_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-column-shift-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-column-shift-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-column-shift-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-column-shift-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientColumnShift");
    editor.rename_sheet("TransientColumnShift", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("rename-back-column-shift-first"));
    sheet.set_cell(2, 3, fastxlsx::CellValue::formula("A1+B2"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before column-shift recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before column shift should keep both handles clean");
    const fastxlsx::CellValue saved_text = reacquired.get_cell(1, 1);
    check(saved_text.kind() == fastxlsx::CellValueKind::Text &&
            saved_text.text_value() == "rename-back-column-shift-first",
        "matching reacquire before column shift should reuse saved text state");
    const fastxlsx::CellValue saved_formula = reacquired.get_cell(2, 3);
    check(saved_formula.kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula.text_value() == "A1+B2",
        "matching reacquire before column shift should reuse saved formula state");
    check(!editor.last_edit_error().has_value(),
        "rename-back column shift recovery should not create last_edit_error");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire before column shift should remain clean");
    matching.insert_columns(2, 1);

    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "post-reacquire column shift should dirty all shared handles");
    check(matching.get_cell("A1").text_value() == "rename-back-column-shift-first",
        "post-reacquire column shift should preserve columns before the insertion point");
    check(!matching.try_cell("B1").has_value(),
        "post-reacquire column shift should remove the old source-backed number coordinate");
    const fastxlsx::CellValue shifted_number = matching.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "post-reacquire column shift should move source-backed number cells");
    check(!matching.try_cell("C2").has_value(),
        "post-reacquire column shift should remove the old formula coordinate");
    const fastxlsx::CellValue shifted_formula = matching.get_cell("D2");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula.text_value() == "B1+C2",
        "post-reacquire column shift should translate the moved formula text");
    check_retry_cell_range_equals(matching.used_range(), 1, 1, 2, 4,
        "post-reacquire column shift should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_one =
        matching.row_cells(1);
    check(shifted_row_one.size() == 2 &&
            shifted_row_one[0].reference.row == 1 &&
            shifted_row_one[0].reference.column == 1 &&
            shifted_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_one[0].value.text_value() == "rename-back-column-shift-first" &&
            shifted_row_one[1].reference.row == 1 &&
            shifted_row_one[1].reference.column == 3 &&
            shifted_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            shifted_row_one[1].value.number_value() == 1.0,
        "post-reacquire column shift row_cells should expose shifted source-backed cells");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_four =
        matching.column_cells(4);
    check(shifted_column_four.size() == 1 &&
            shifted_column_four[0].reference.row == 2 &&
            shifted_column_four[0].reference.column == 4 &&
            shifted_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_four[0].value.text_value() == "B1+C2",
        "post-reacquire column shift column_cells should expose the shifted formula cell");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "post-reacquire column shift dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "post-reacquire column shift should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "post-reacquire column shift summary should use restored names");
            check(!summary.renamed,
                "post-reacquire column shift summary should not be marked renamed");
            check(summary.materialized_dirty,
                "post-reacquire column shift summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "post-reacquire column shift summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all column-shift recovery handles");
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "second safe column-shift save");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first column-shift recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientColumnShift",
        "first column-shift recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-column-shift-first",
        "first output should contain the saved text before the column shift");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1"><v>1</v></c>)",
        "first output should contain the source-backed number before the column shift");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C2"><f>A1+B2</f></c>)",
        "first output should contain the saved formula before the column shift");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="D2"><f>B1+C2</f></c>)",
        "first output should not contain the later column-shifted formula");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second column-shift recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientColumnShift",
        "second column-shift recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-column-shift-first",
        "second output should preserve the saved text after the column shift");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C1"><v>1</v></c>)",
        "second output should persist the shifted source-backed number");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="D2"><f>B1+C2</f></c>)",
        "second output should persist the column-shifted translated formula");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1"><v>1</v></c>)",
        "second output should not keep the old number coordinate");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C2"><f>A1+B2</f></c>)",
        "second output should not keep the old formula coordinate");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "TransientColumnShift",
        "second column-shift worksheet output should not leak the transient planned name");

    fastxlsx::WorkbookEditor reopened_editor =
        fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        reopened_editor, reopened_sheet, "reopened column-shift output");
    check(reopened_sheet.get_cell("A1").text_value() ==
            "rename-back-column-shift-first",
        "reopened column-shift output should read back preserved first-column text");
    check(!reopened_sheet.try_cell("B1").has_value(),
        "reopened column-shift output should not read back old number coordinate");
    const fastxlsx::CellValue reopened_number = reopened_sheet.get_cell("C1");
    check(reopened_number.kind() == fastxlsx::CellValueKind::Number &&
            reopened_number.number_value() == 1.0,
        "reopened column-shift output should read back shifted source-backed number");
    const fastxlsx::CellValue reopened_formula = reopened_sheet.get_cell("D2");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == "B1+C2",
        "reopened column-shift output should read back translated formula");
    check_retry_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
        "reopened column-shift output should read back shifted sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
        reopened_sheet.row_cells(1);
    check(reopened_row_one.size() == 2 &&
            reopened_row_one[0].reference.row == 1 &&
            reopened_row_one[0].reference.column == 1 &&
            reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_row_one[0].value.text_value() ==
                "rename-back-column-shift-first" &&
            reopened_row_one[1].reference.row == 1 &&
            reopened_row_one[1].reference.column == 3 &&
            reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_row_one[1].value.number_value() == 1.0,
        "reopened column-shift row_cells should expose shifted sparse cells");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
        reopened_sheet.column_cells(4);
    check(reopened_column_four.size() == 1 &&
            reopened_column_four[0].reference.row == 2 &&
            reopened_column_four[0].reference.column == 4 &&
            reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_column_four[0].value.text_value() == "B1+C2",
        "reopened column-shift column_cells should expose shifted formula");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "column-shift no-op save should keep all recovery handles clean");
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "column-shift no-op save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "column-shift no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "column-shift no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "column-shift no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "column-shift no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_editor =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor noop_sheet =
        noop_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        noop_editor, noop_sheet, "column-shift no-op reopened output");
    check(noop_sheet.get_cell("A1").text_value() ==
            "rename-back-column-shift-first",
        "column-shift no-op reopened output should read back preserved first-column text");
    check(!noop_sheet.try_cell("B1").has_value(),
        "column-shift no-op reopened output should not read back old number coordinate");
    const fastxlsx::CellValue noop_number = noop_sheet.get_cell("C1");
    check(noop_number.kind() == fastxlsx::CellValueKind::Number &&
            noop_number.number_value() == 1.0,
        "column-shift no-op reopened output should read back shifted source-backed number");
    check(!noop_sheet.try_cell("C2").has_value(),
        "column-shift no-op reopened output should not read back old formula coordinate");
    const fastxlsx::CellValue noop_formula = noop_sheet.get_cell("D2");
    check(noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula.text_value() == "B1+C2",
        "column-shift no-op reopened output should read back translated formula");
    check_retry_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 4,
        "column-shift no-op reopened output should read back shifted sparse used range");
}

void test_public_worksheet_editor_rename_back_failed_save_as_styled_shift_preserves_reacquired_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-styled-shift-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-styled-shift-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-styled-shift-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-styled-shift-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientStyledShift");
    editor.rename_sheet("TransientStyledShift", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("rename-back-styled-shift-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before styled shift recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before styled row shift should keep both handles clean");
    const fastxlsx::CellValue saved_formula = reacquired.get_cell("D2");
    check(saved_formula.kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula.text_value() == "A1+B1" &&
            saved_formula.has_style() &&
            saved_formula.style_id().value() == styled_formula_style.value(),
        "matching reacquire before styled row shift should reuse saved formula style");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    matching.insert_rows(2, 2);

    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "post-reacquire styled row shift should dirty all shared handles");
    const fastxlsx::CellValue shifted_formula = matching.get_cell("D4");
    check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula.text_value() == "A3+B3" &&
            shifted_formula.has_style() &&
            shifted_formula.style_id().value() == styled_formula_style.value(),
        "post-reacquire row shift should translate formula text and preserve style id");
    check_retry_cell_range_equals(matching.used_range(), 1, 1, 5, 4,
        "post-reacquire styled row shift should refresh the in-memory sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_four =
        matching.row_cells(4);
    check(shifted_row_four.size() == 4 &&
            shifted_row_four[3].reference.row == 4 &&
            shifted_row_four[3].reference.column == 4 &&
            shifted_row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_four[3].value.text_value() == "A3+B3" &&
            shifted_row_four[3].value.has_style() &&
            shifted_row_four[3].value.style_id().value() == styled_formula_style.value(),
        "post-reacquire styled row shift row_cells should expose formula style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_four =
        matching.column_cells(4);
    check(shifted_column_four.size() == 1 &&
            shifted_column_four[0].reference.row == 4 &&
            shifted_column_four[0].reference.column == 4 &&
            shifted_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_four[0].value.text_value() == "A3+B3" &&
            shifted_column_four[0].value.has_style() &&
            shifted_column_four[0].value.style_id().value() == styled_formula_style.value(),
        "post-reacquire styled row shift column_cells should expose formula style id");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "post-reacquire styled row shift dirty diagnostics should use restored source name");
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all styled shift recovery handles");
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "second safe styled row-shift save");

    const std::string saved_formula_xml =
        std::string(R"(<c r="D2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    const std::string shifted_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A3+B3</f></c>)";

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first styled shift recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientStyledShift",
        "first styled shift recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"), saved_formula_xml,
        "first output should contain the saved styled formula before row shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second styled shift recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientStyledShift",
        "second styled shift recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"), shifted_formula_xml,
        "second output should persist shifted formula text with the preserved style id");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), saved_formula_xml,
        "second output should not keep the old styled formula coordinate");
    check_contains(second_entries.at("xl/styles.xml"), R"(numFmtId=")",
        "second output should preserve the source workbook styles part");

    fastxlsx::WorkbookEditor reopened_editor =
        fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        reopened_editor, reopened_sheet, "reopened styled shift output");
    const fastxlsx::CellValue reopened_formula = reopened_sheet.get_cell("D4");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == "A3+B3" &&
            reopened_formula.has_style() &&
            reopened_formula.style_id().value() == styled_formula_style.value(),
        "reopened styled shift output should read back shifted formula style");
    check_retry_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
        "reopened styled shift output should read back the shifted sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_four =
        reopened_sheet.row_cells(4);
    check(reopened_row_four.size() == 4 &&
            reopened_row_four[3].reference.row == 4 &&
            reopened_row_four[3].reference.column == 4 &&
            reopened_row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_row_four[3].value.text_value() == "A3+B3" &&
            reopened_row_four[3].value.has_style() &&
            reopened_row_four[3].value.style_id().value() == styled_formula_style.value(),
        "reopened styled shift row_cells should expose shifted formula style");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
        reopened_sheet.column_cells(4);
    check(reopened_column_four.size() == 1 &&
            reopened_column_four[0].reference.row == 4 &&
            reopened_column_four[0].reference.column == 4 &&
            reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_column_four[0].value.text_value() == "A3+B3" &&
            reopened_column_four[0].value.has_style() &&
            reopened_column_four[0].value.style_id().value() == styled_formula_style.value(),
        "reopened styled shift column_cells should expose shifted formula style");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "styled row-shift no-op save should keep all recovery handles clean");
    check_retry_reacquire_safe_save_clean_state(
        editor, 4, "styled row-shift no-op save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "styled row-shift no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "styled row-shift no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "styled row-shift no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled row-shift no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_editor =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor noop_sheet =
        noop_editor.worksheet("Data", options);
    check_retry_reopened_clean_state(
        noop_editor, noop_sheet, "styled row-shift no-op reopened output");
    const fastxlsx::CellValue noop_formula = noop_sheet.get_cell("D4");
    check(noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula.text_value() == "A3+B3" &&
            noop_formula.has_style() &&
            noop_formula.style_id().value() == styled_formula_style.value(),
        "styled row-shift no-op reopened output should read back shifted formula style");
    check_retry_cell_range_equals(noop_sheet.used_range(), 1, 1, 5, 4,
        "styled row-shift no-op reopened output should read back shifted sparse used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> noop_row_four =
        noop_sheet.row_cells(4);
    check(noop_row_four.size() == 4 &&
            noop_row_four[3].reference.row == 4 &&
            noop_row_four[3].reference.column == 4 &&
            noop_row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            noop_row_four[3].value.text_value() == "A3+B3" &&
            noop_row_four[3].value.has_style() &&
            noop_row_four[3].value.style_id().value() == styled_formula_style.value(),
        "styled row-shift no-op row_cells should expose shifted formula style");
    const std::vector<fastxlsx::WorksheetCellSnapshot> noop_column_four =
        noop_sheet.column_cells(4);
    check(noop_column_four.size() == 1 &&
            noop_column_four[0].reference.row == 4 &&
            noop_column_four[0].reference.column == 4 &&
            noop_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            noop_column_four[0].value.text_value() == "A3+B3" &&
            noop_column_four[0].value.has_style() &&
            noop_column_four[0].value.style_id().value() == styled_formula_style.value(),
        "styled row-shift no-op column_cells should expose shifted formula style");
}

void test_public_worksheet_editor_rename_back_failed_save_as_delete_shifts_preserve_reacquired_state()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-row-shift-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-row-shift-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-row-shift-second.xlsx");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.rename_sheet("Data", "TransientDeleteRowShift");
        editor.rename_sheet("TransientDeleteRowShift", "Data");

        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        sheet.set_cell(1, 1, fastxlsx::CellValue::text("rename-back-delete-row-shift-first"));
        sheet.set_cell(4, 3, fastxlsx::CellValue::formula("A2+B4"));
        sheet.set_cell(4, 2, fastxlsx::CellValue::text("tail-b4"));

        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "source-overwrite save_as should reject before delete-row recovery setup flushes");
        editor.save_as(first_output);

        fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
            "matching reacquire before delete_rows should keep both handles clean");
        check(reacquired.get_cell("A1").text_value() ==
                "rename-back-delete-row-shift-first",
            "matching reacquire before delete_rows should reuse saved text state");
        check(reacquired.get_cell("C4").text_value() == "A2+B4",
            "matching reacquire before delete_rows should reuse saved formula text");

        fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
        matching.delete_rows(1, 1);

        check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
                matching.has_pending_changes(),
            "post-reacquire delete_rows should dirty all shared handles");
        check(matching.get_cell("A1").text_value() == "placeholder-a2",
            "post-reacquire delete_rows should shift source-backed rows upward");
        check(!matching.try_cell("B1").has_value(),
            "post-reacquire delete_rows should remove deleted row number cells");
        check(matching.get_cell("B3").text_value() == "tail-b4",
            "post-reacquire delete_rows should shift dirty rows upward");
        check(!matching.try_cell("C4").has_value(),
            "post-reacquire delete_rows should remove the old formula coordinate");
        const fastxlsx::CellValue shifted_formula = matching.get_cell("C3");
        check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula.text_value() == "A1+B3",
            "post-reacquire delete_rows should translate the moved formula text");
        check_retry_cell_range_equals(matching.used_range(), 1, 1, 3, 3,
            "post-reacquire delete_rows should refresh the in-memory sparse used range");
        const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_three =
            matching.row_cells(3);
        check(shifted_row_three.size() == 2 &&
                shifted_row_three[0].reference.row == 3 &&
                shifted_row_three[0].reference.column == 2 &&
                shifted_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                shifted_row_three[0].value.text_value() == "tail-b4" &&
                shifted_row_three[1].reference.row == 3 &&
                shifted_row_three[1].reference.column == 3 &&
                shifted_row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_row_three[1].value.text_value() == "A1+B3",
            "post-reacquire delete_rows row_cells should expose shifted dirty and formula cells");
        const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_three =
            matching.column_cells(3);
        check(shifted_column_three.size() == 1 &&
                shifted_column_three[0].reference.row == 3 &&
                shifted_column_three[0].reference.column == 3 &&
                shifted_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_column_three[0].value.text_value() == "A1+B3",
            "post-reacquire delete_rows column_cells should expose the shifted formula cell");
        {
            const std::vector<std::string> names =
                editor.pending_materialized_worksheet_names();
            check(names.size() == 1 && names[0] == "Data",
                "post-reacquire delete_rows dirty diagnostics should use restored source name");
        }
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                "post-reacquire delete_rows should create one dirty summary");
            if (summaries.size() == 1) {
                const auto& summary = summaries[0];
                check(summary.source_name == "Data" && summary.planned_name == "Data",
                    "post-reacquire delete_rows summary should use restored names");
                check(!summary.renamed,
                    "post-reacquire delete_rows summary should not be marked renamed");
                check(summary.materialized_dirty,
                    "post-reacquire delete_rows summary should report dirty materialized state");
                check(!summary.sheet_data_replaced,
                    "post-reacquire delete_rows summary should not invent replacement diagnostics");
            }
        }

        editor.save_as(second_output);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
                !matching.has_pending_changes(),
            "second safe save_as should clean all delete-row recovery handles");
        check_retry_reacquire_safe_save_clean_state(
            editor, 4, "second safe delete-row save");

        const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
        check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
            "first delete-row recovery output should use the restored source name");
        check_not_contains(first_entries.at("xl/workbook.xml"), "TransientDeleteRowShift",
            "first delete-row recovery output should not leak the transient planned name");
        check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
            "rename-back-delete-row-shift-first",
            "first output should contain the saved row-one text before delete_rows");
        check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="C4"><f>A2+B4</f></c>)",
            "first output should contain the saved formula before delete_rows");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
            "second delete-row recovery output should keep the restored source name");
        check_not_contains(second_entries.at("xl/workbook.xml"), "TransientDeleteRowShift",
            "second delete-row recovery output should not leak the transient planned name");
        check_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
            "second output should persist the shifted source-backed row");
        check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="C3"><f>A1+B3</f></c>)",
            "second output should persist the delete-row translated formula");
        check_contains(second_entries.at("xl/worksheets/sheet1.xml"), R"(<c r="B3")",
            "second output should persist the delete-row shifted dirty cell");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            "rename-back-delete-row-shift-first",
            "second output should omit text from the deleted row");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
            "second output should omit deleted source row text");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="B1"><v>1</v></c>)",
            "second output should omit deleted source row number");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="C4"><f>A2+B4</f></c>)",
            "second output should not keep the old delete-row formula coordinate");

        fastxlsx::WorkbookEditor reopened_editor =
            fastxlsx::WorkbookEditor::open(second_output);
        fastxlsx::WorksheetEditor reopened_sheet =
            reopened_editor.worksheet("Data", options);
        check_retry_reopened_clean_state(
            reopened_editor, reopened_sheet, "reopened delete-row output");
        check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2",
            "reopened delete-row output should read back shifted source-backed row");
        check(!reopened_sheet.try_cell("B1").has_value(),
            "reopened delete-row output should not read back deleted row number");
        check(reopened_sheet.get_cell("B3").text_value() == "tail-b4",
            "reopened delete-row output should read back shifted dirty cell");
        const fastxlsx::CellValue reopened_formula =
            reopened_sheet.get_cell("C3");
        check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_formula.text_value() == "A1+B3",
            "reopened delete-row output should read back translated formula");
        check_retry_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
            "reopened delete-row output should read back shifted sparse used range");
        const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
            reopened_sheet.row_cells(3);
        check(reopened_row_three.size() == 2 &&
                reopened_row_three[0].reference.row == 3 &&
                reopened_row_three[0].reference.column == 2 &&
                reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                reopened_row_three[0].value.text_value() == "tail-b4" &&
                reopened_row_three[1].reference.row == 3 &&
                reopened_row_three[1].reference.column == 3 &&
                reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_row_three[1].value.text_value() == "A1+B3",
            "reopened delete-row row_cells should expose shifted sparse cells");
        const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
            reopened_sheet.column_cells(3);
        check(reopened_column_three.size() == 1 &&
                reopened_column_three[0].reference.row == 3 &&
                reopened_column_three[0].reference.column == 3 &&
                reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_column_three[0].value.text_value() == "A1+B3",
            "reopened delete-row column_cells should expose shifted formula");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-column-shift-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-column-shift-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-column-shift-second.xlsx");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.rename_sheet("Data", "TransientDeleteColumnShift");
        editor.rename_sheet("TransientDeleteColumnShift", "Data");

        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        sheet.set_cell(1, 1,
            fastxlsx::CellValue::text("rename-back-delete-column-shift-first"));
        sheet.set_cell(1, 3, fastxlsx::CellValue::formula("B2+D1"));
        sheet.set_cell(2, 4, fastxlsx::CellValue::text("tail-d2"));

        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "source-overwrite save_as should reject before delete-column recovery setup flushes");
        editor.save_as(first_output);

        fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
            "matching reacquire before delete_columns should keep both handles clean");
        check(reacquired.get_cell("A1").text_value() ==
                "rename-back-delete-column-shift-first",
            "matching reacquire before delete_columns should reuse saved text state");
        check(reacquired.get_cell("C1").text_value() == "B2+D1",
            "matching reacquire before delete_columns should reuse saved formula text");

        fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
        matching.delete_columns(1, 1);

        check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
                matching.has_pending_changes(),
            "post-reacquire delete_columns should dirty all shared handles");
        check(matching.get_cell("A1").number_value() == 1.0,
            "post-reacquire delete_columns should shift source-backed columns left");
        check(!matching.try_cell("A2").has_value(),
            "post-reacquire delete_columns should remove deleted column text cells");
        check(matching.get_cell("C2").text_value() == "tail-d2",
            "post-reacquire delete_columns should shift dirty columns left");
        check(!matching.try_cell("C1").has_value(),
            "post-reacquire delete_columns should remove the old formula coordinate");
        const fastxlsx::CellValue shifted_formula = matching.get_cell("B1");
        check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula.text_value() == "A2+C1",
            "post-reacquire delete_columns should translate the moved formula text");
        check_retry_cell_range_equals(matching.used_range(), 1, 1, 2, 3,
            "post-reacquire delete_columns should refresh the in-memory sparse used range");
        const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_one =
            matching.row_cells(1);
        check(shifted_row_one.size() == 2 &&
                shifted_row_one[0].reference.row == 1 &&
                shifted_row_one[0].reference.column == 1 &&
                shifted_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                shifted_row_one[0].value.number_value() == 1.0 &&
                shifted_row_one[1].reference.row == 1 &&
                shifted_row_one[1].reference.column == 2 &&
                shifted_row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_row_one[1].value.text_value() == "A2+C1",
            "post-reacquire delete_columns row_cells should expose shifted number and formula cells");
        const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_two =
            matching.column_cells(2);
        check(shifted_column_two.size() == 1 &&
                shifted_column_two[0].reference.row == 1 &&
                shifted_column_two[0].reference.column == 2 &&
                shifted_column_two[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_column_two[0].value.text_value() == "A2+C1",
            "post-reacquire delete_columns column_cells should expose the shifted formula cell");
        {
            const std::vector<std::string> names =
                editor.pending_materialized_worksheet_names();
            check(names.size() == 1 && names[0] == "Data",
                "post-reacquire delete_columns dirty diagnostics should use restored source name");
        }
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                "post-reacquire delete_columns should create one dirty summary");
            if (summaries.size() == 1) {
                const auto& summary = summaries[0];
                check(summary.source_name == "Data" && summary.planned_name == "Data",
                    "post-reacquire delete_columns summary should use restored names");
                check(!summary.renamed,
                    "post-reacquire delete_columns summary should not be marked renamed");
                check(summary.materialized_dirty,
                    "post-reacquire delete_columns summary should report dirty materialized state");
                check(!summary.sheet_data_replaced,
                    "post-reacquire delete_columns summary should not invent replacement diagnostics");
            }
        }

        editor.save_as(second_output);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
                !matching.has_pending_changes(),
            "second safe save_as should clean all delete-column recovery handles");
        check_retry_reacquire_safe_save_clean_state(
            editor, 4, "second safe delete-column save");

        const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
        check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
            "first delete-column recovery output should use the restored source name");
        check_not_contains(first_entries.at("xl/workbook.xml"), "TransientDeleteColumnShift",
            "first delete-column recovery output should not leak the transient planned name");
        check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
            "rename-back-delete-column-shift-first",
            "first output should contain the saved first-column text before delete_columns");
        check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="B1"><v>1</v></c>)",
            "first output should contain the source-backed number before delete_columns");
        check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="C1"><f>B2+D1</f></c>)",
            "first output should contain the saved formula before delete_columns");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
            "second delete-column recovery output should keep the restored source name");
        check_not_contains(second_entries.at("xl/workbook.xml"), "TransientDeleteColumnShift",
            "second delete-column recovery output should not leak the transient planned name");
        check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="A1"><v>1</v></c>)",
            "second output should persist the shifted source-backed number");
        check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="B1"><f>A2+C1</f></c>)",
            "second output should persist the delete-column translated formula");
        check_contains(second_entries.at("xl/worksheets/sheet1.xml"), R"(<c r="C2")",
            "second output should persist the delete-column shifted dirty cell");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            "rename-back-delete-column-shift-first",
            "second output should omit text from the deleted column");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
            "second output should omit deleted column row-one text");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
            "second output should omit deleted column row-two text");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="C1"><f>B2+D1</f></c>)",
            "second output should not keep the old delete-column formula coordinate");

        fastxlsx::WorkbookEditor reopened_editor =
            fastxlsx::WorkbookEditor::open(second_output);
        fastxlsx::WorksheetEditor reopened_sheet =
            reopened_editor.worksheet("Data", options);
        check_retry_reopened_clean_state(
            reopened_editor, reopened_sheet, "reopened delete-column output");
        const fastxlsx::CellValue reopened_number =
            reopened_sheet.get_cell("A1");
        check(reopened_number.kind() == fastxlsx::CellValueKind::Number &&
                reopened_number.number_value() == 1.0,
            "reopened delete-column output should read back shifted source-backed number");
        check(!reopened_sheet.try_cell("A2").has_value(),
            "reopened delete-column output should not read back deleted column text");
        check(reopened_sheet.get_cell("C2").text_value() == "tail-d2",
            "reopened delete-column output should read back shifted dirty cell");
        const fastxlsx::CellValue reopened_formula =
            reopened_sheet.get_cell("B1");
        check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_formula.text_value() == "A2+C1",
            "reopened delete-column output should read back translated formula");
        check_retry_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
            "reopened delete-column output should read back shifted sparse used range");
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
            "reopened delete-column row_cells should expose shifted sparse cells");
        const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_two =
            reopened_sheet.column_cells(2);
        check(reopened_column_two.size() == 1 &&
                reopened_column_two[0].reference.row == 1 &&
                reopened_column_two[0].reference.column == 2 &&
                reopened_column_two[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_column_two[0].value.text_value() == "A2+C1",
            "reopened delete-column column_cells should expose shifted formula");
    }
}

void test_public_worksheet_editor_rename_back_failed_save_as_delete_ref_formula_shifts_preserve_reacquired_state()
{
    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-row-ref-formula-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-row-ref-formula-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-row-ref-formula-second.xlsx");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.rename_sheet("Data", "TransientDeleteRowRefFormula");
        editor.rename_sheet("TransientDeleteRowRefFormula", "Data");

        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        sheet.set_cell(4, 3, fastxlsx::CellValue::formula("A1+A:A+1:1+B4"));

        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "source-overwrite save_as should reject before delete-row ref formula recovery setup flushes");
        editor.save_as(first_output);

        fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
            "matching reacquire before delete_rows ref formula shift should keep both handles clean");
        const fastxlsx::CellValue saved_formula = reacquired.get_cell("C4");
        check(saved_formula.kind() == fastxlsx::CellValueKind::Formula &&
                saved_formula.text_value() == "A1+A:A+1:1+B4",
            "matching reacquire before delete_rows ref formula shift should reuse saved formula text");
        check(!editor.last_edit_error().has_value(),
            "delete-row ref formula recovery should not create last_edit_error");

        fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
        matching.delete_rows(1, 1);

        check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
                matching.has_pending_changes(),
            "post-reacquire delete_rows ref formula shift should dirty all shared handles");
        check(matching.get_cell("A1").text_value() == "placeholder-a2",
            "post-reacquire delete_rows ref formula shift should move source-backed rows");
        check(!matching.try_cell("C4").has_value(),
            "post-reacquire delete_rows ref formula shift should remove the old formula coordinate");
        const fastxlsx::CellValue shifted_formula = matching.get_cell("C3");
        check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula.text_value() == "#REF!+A:A+#REF!+B3",
            "post-reacquire delete_rows should translate row-out-of-bounds formula references to #REF!");

        editor.save_as(second_output);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
                !matching.has_pending_changes(),
            "second safe save_as should clean delete-row ref formula recovery handles");
        check_retry_reacquire_safe_save_clean_state(
            editor, 4, "second safe delete-row ref formula save");

        const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
        check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
            "first delete-row ref formula recovery output should use the restored source name");
        check_not_contains(first_entries.at("xl/workbook.xml"), "TransientDeleteRowRefFormula",
            "first delete-row ref formula recovery output should not leak the transient planned name");
        check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="C4"><f>A1+A:A+1:1+B4</f></c>)",
            "first output should contain the saved formula before delete_rows ref formula shift");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
            "second delete-row ref formula recovery output should keep the restored source name");
        check_not_contains(second_entries.at("xl/workbook.xml"), "TransientDeleteRowRefFormula",
            "second delete-row ref formula recovery output should not leak the transient planned name");
        check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="C3"><f>#REF!+A:A+#REF!+B3</f></c>)",
            "second output should persist delete-row #REF formula translation");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="C4"><f>A1+A:A+1:1+B4</f></c>)",
            "second output should not keep the old delete-row ref formula coordinate");

        fastxlsx::WorkbookEditor reopened_editor =
            fastxlsx::WorkbookEditor::open(second_output);
        fastxlsx::WorksheetEditor reopened_sheet =
            reopened_editor.worksheet("Data", options);
        check_retry_reopened_clean_state(
            reopened_editor, reopened_sheet, "reopened delete-row ref formula output");
        check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2",
            "reopened delete-row ref formula output should read back shifted source row");
        check(!reopened_sheet.try_cell("C4").has_value(),
            "reopened delete-row ref formula output should not read back old formula coordinate");
        const fastxlsx::CellValue reopened_formula =
            reopened_sheet.get_cell("C3");
        check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_formula.text_value() == "#REF!+A:A+#REF!+B3",
            "reopened delete-row ref formula output should read back #REF! translation");
        check_retry_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
            "reopened delete-row ref formula output should read back shifted sparse used range");
        const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
            reopened_sheet.row_cells(3);
        check(reopened_row_three.size() == 1 &&
                reopened_row_three[0].reference.row == 3 &&
                reopened_row_three[0].reference.column == 3 &&
                reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_row_three[0].value.text_value() == "#REF!+A:A+#REF!+B3",
            "reopened delete-row ref formula row_cells should expose shifted formula");
        const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
            reopened_sheet.column_cells(3);
        check(reopened_column_three.size() == 1 &&
                reopened_column_three[0].reference.row == 3 &&
                reopened_column_three[0].reference.column == 3 &&
                reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_column_three[0].value.text_value() == "#REF!+A:A+#REF!+B3",
            "reopened delete-row ref formula column_cells should expose shifted formula");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-column-ref-formula-source.xlsx");
        const std::filesystem::path first_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-column-ref-formula-first.xlsx");
        const std::filesystem::path second_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-delete-column-ref-formula-second.xlsx");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.rename_sheet("Data", "TransientDeleteColumnRefFormula");
        editor.rename_sheet("TransientDeleteColumnRefFormula", "Data");

        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        sheet.set_cell(1, 4, fastxlsx::CellValue::formula("A1+A:A+1:1+D2"));

        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "source-overwrite save_as should reject before delete-column ref formula recovery setup flushes");
        editor.save_as(first_output);

        fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
            "matching reacquire before delete_columns ref formula shift should keep both handles clean");
        const fastxlsx::CellValue saved_formula = reacquired.get_cell("D1");
        check(saved_formula.kind() == fastxlsx::CellValueKind::Formula &&
                saved_formula.text_value() == "A1+A:A+1:1+D2",
            "matching reacquire before delete_columns ref formula shift should reuse saved formula text");
        check(!editor.last_edit_error().has_value(),
            "delete-column ref formula recovery should not create last_edit_error");

        fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
        matching.delete_columns(1, 1);

        check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
                matching.has_pending_changes(),
            "post-reacquire delete_columns ref formula shift should dirty all shared handles");
        const fastxlsx::CellValue shifted_number = matching.get_cell("A1");
        check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
                shifted_number.number_value() == 1.0,
            "post-reacquire delete_columns ref formula shift should move source-backed columns");
        check(!matching.try_cell("D1").has_value(),
            "post-reacquire delete_columns ref formula shift should remove the old formula coordinate");
        const fastxlsx::CellValue shifted_formula = matching.get_cell("C1");
        check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula.text_value() == "#REF!+#REF!+1:1+C2",
            "post-reacquire delete_columns should translate column-out-of-bounds formula references to #REF!");

        editor.save_as(second_output);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
                !matching.has_pending_changes(),
            "second safe save_as should clean delete-column ref formula recovery handles");
        check_retry_reacquire_safe_save_clean_state(
            editor, 4, "second safe delete-column ref formula save");

        const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
        check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
            "first delete-column ref formula recovery output should use the restored source name");
        check_not_contains(first_entries.at("xl/workbook.xml"), "TransientDeleteColumnRefFormula",
            "first delete-column ref formula recovery output should not leak the transient planned name");
        check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="D1"><f>A1+A:A+1:1+D2</f></c>)",
            "first output should contain the saved formula before delete_columns ref formula shift");

        const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
        check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
            "second delete-column ref formula recovery output should keep the restored source name");
        check_not_contains(second_entries.at("xl/workbook.xml"), "TransientDeleteColumnRefFormula",
            "second delete-column ref formula recovery output should not leak the transient planned name");
        check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="C1"><f>#REF!+#REF!+1:1+C2</f></c>)",
            "second output should persist delete-column #REF formula translation");
        check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
            R"(<c r="D1"><f>A1+A:A+1:1+D2</f></c>)",
            "second output should not keep the old delete-column ref formula coordinate");

        fastxlsx::WorkbookEditor reopened_editor =
            fastxlsx::WorkbookEditor::open(second_output);
        fastxlsx::WorksheetEditor reopened_sheet =
            reopened_editor.worksheet("Data", options);
        check_retry_reopened_clean_state(
            reopened_editor, reopened_sheet, "reopened delete-column ref formula output");
        const fastxlsx::CellValue reopened_number =
            reopened_sheet.get_cell("A1");
        check(reopened_number.kind() == fastxlsx::CellValueKind::Number &&
                reopened_number.number_value() == 1.0,
            "reopened delete-column ref formula output should read back shifted source number");
        check(!reopened_sheet.try_cell("D1").has_value(),
            "reopened delete-column ref formula output should not read back old formula coordinate");
        const fastxlsx::CellValue reopened_formula =
            reopened_sheet.get_cell("C1");
        check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_formula.text_value() == "#REF!+#REF!+1:1+C2",
            "reopened delete-column ref formula output should read back #REF! translation");
        check_retry_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 3,
            "reopened delete-column ref formula output should read back shifted sparse used range");
        const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
            reopened_sheet.row_cells(1);
        check(reopened_row_one.size() == 2 &&
                reopened_row_one[0].reference.row == 1 &&
                reopened_row_one[0].reference.column == 1 &&
                reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                reopened_row_one[0].value.number_value() == 1.0 &&
                reopened_row_one[1].reference.row == 1 &&
                reopened_row_one[1].reference.column == 3 &&
                reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_row_one[1].value.text_value() == "#REF!+#REF!+1:1+C2",
            "reopened delete-column ref formula row_cells should expose shifted cells");
        const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
            reopened_sheet.column_cells(3);
        check(reopened_column_three.size() == 1 &&
                reopened_column_three[0].reference.row == 1 &&
                reopened_column_three[0].reference.column == 3 &&
                reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                reopened_column_three[0].value.text_value() == "#REF!+#REF!+1:1+C2",
            "reopened delete-column ref formula column_cells should expose shifted formula");
    }
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
            test_public_worksheet_editor_rename_back_failed_save_as_shift_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_column_shift_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_styled_shift_preserves_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_delete_shifts_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_delete_ref_formula_shifts_preserve_reacquired_state();
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
