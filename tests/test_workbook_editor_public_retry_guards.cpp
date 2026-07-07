#include "test_workbook_editor_public_retry_common.hpp"

void check_reopened_guard_recovery_materialized_output(
    const std::filesystem::path& output,
    const fastxlsx::WorksheetEditorOptions& options,
    std::string_view context,
    std::string_view saved_text,
    std::string_view later_text)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);
    const std::string prefix(context);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened output should not expose pending edits");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " reopened output should not expose dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_editor, prefix + " reopened output");
    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened output should keep diagnostics clear");
    check(reopened_sheet.cell_count() == 4,
        prefix + " reopened output should keep the saved sparse cell count");

    const fastxlsx::CellValue reopened_saved = reopened_sheet.get_cell("A1");
    check(reopened_saved.kind() == fastxlsx::CellValueKind::Text &&
            reopened_saved.text_value() == saved_text,
        prefix + " reopened output should read back the saved A1 value");
    const fastxlsx::CellValue reopened_source = reopened_sheet.get_cell("A2");
    check(reopened_source.kind() == fastxlsx::CellValueKind::Text &&
            reopened_source.text_value() == "placeholder-a2",
        prefix + " reopened output should keep source-backed A2");
    const fastxlsx::CellValue reopened_later = reopened_sheet.get_cell("B2");
    check(reopened_later.kind() == fastxlsx::CellValueKind::Text &&
            reopened_later.text_value() == later_text,
        prefix + " reopened output should read back the later B2 edit");

    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
        reopened_sheet.row_cells(2);
    check(reopened_row_two.size() == 2 &&
            reopened_row_two[0].reference.row == 2 &&
            reopened_row_two[0].reference.column == 1 &&
            reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_row_two[0].value.text_value() == "placeholder-a2" &&
            reopened_row_two[1].reference.row == 2 &&
            reopened_row_two[1].reference.column == 2 &&
            reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_row_two[1].value.text_value() == later_text,
        prefix + " reopened row_cells should expose source-backed and later edits");
}

void test_public_worksheet_editor_rename_back_failed_save_as_handle_reads_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-handle-reads-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-handle-reads-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-handle-reads-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-handle-reads-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientHandleReads");
    editor.rename_sheet("TransientHandleReads", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-handle-reads-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before handle-read recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before handle reads should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const std::optional<fastxlsx::CellValue> maybe_saved = sheet.try_cell(1, 1);
    check(maybe_saved.has_value() &&
            maybe_saved->kind() == fastxlsx::CellValueKind::Text &&
            maybe_saved->text_value() == "rename-back-handle-reads-first",
        "post-recovery try_cell should read the saved materialized value");
    const std::optional<fastxlsx::CellValue> maybe_saved_a1 =
        reacquired.try_cell("A1");
    check(maybe_saved_a1.has_value() &&
            maybe_saved_a1->kind() == fastxlsx::CellValueKind::Text &&
            maybe_saved_a1->text_value() == "rename-back-handle-reads-first",
        "post-recovery A1 try_cell should read the saved materialized value");
    check(!reacquired.try_cell(9, 9).has_value(),
        "post-recovery try_cell should not synthesize missing cells");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(9, 9); }),
        "post-recovery get_cell should still throw for missing cells");

    const fastxlsx::CellValue saved_value = reacquired.get_cell(1, 1);
    check(saved_value.kind() == fastxlsx::CellValueKind::Text &&
            saved_value.text_value() == "rename-back-handle-reads-first",
        "post-recovery get_cell should preserve the saved materialized value");
    const fastxlsx::CellValue source_value = sheet.get_cell("A2");
    check(source_value.kind() == fastxlsx::CellValueKind::Text &&
            source_value.text_value() == "placeholder-a2",
        "post-recovery get_cell should keep unchanged source-backed cells");
    check(sheet.cell_count() == 3 && reacquired.cell_count() == 3,
        "post-recovery cell_count should report the clean saved sparse store");
    check(sheet.estimated_memory_usage() > 0 &&
            sheet.estimated_memory_usage() == reacquired.estimated_memory_usage(),
        "post-recovery estimated_memory_usage should read the shared saved store");

    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
            reacquired.sparse_cells();
        check(cells.size() == 3,
            "post-recovery sparse_cells should snapshot the saved sparse store");
        if (cells.size() == 3) {
            check(cells[0].reference.row == 1 && cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[0].value.text_value() == "rename-back-handle-reads-first",
                "post-recovery sparse_cells should expose saved A1 first");
            check(cells[1].reference.row == 1 && cells[1].reference.column == 2 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    cells[1].value.number_value() == 1.0,
                "post-recovery sparse_cells should preserve source-backed B1");
            check(cells[2].reference.row == 2 && cells[2].reference.column == 1 &&
                    cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[2].value.text_value() == "placeholder-a2",
                "post-recovery sparse_cells should preserve source-backed A2");
        }
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 2});
        check(range_cells.size() == 2,
            "post-recovery sparse_cells(range) should snapshot only requested records");
        if (range_cells.size() == 2) {
            check(range_cells[0].reference.row == 1 &&
                    range_cells[0].reference.column == 1 &&
                    range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    range_cells[0].value.text_value() == "rename-back-handle-reads-first",
                "post-recovery sparse_cells(range) should expose saved A1");
            check(range_cells[1].reference.row == 1 &&
                    range_cells[1].reference.column == 2 &&
                    range_cells[1].value.kind() == fastxlsx::CellValueKind::Number,
                "post-recovery sparse_cells(range) should expose source-backed B1");
        }
    }

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-handle-reads-first",
        "TransientHandleReads",
        "post-recovery handle reads",
        3);

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after handle reads should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-handle-reads-first",
        "matching reacquire after handle reads should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-handle-reads-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-handle-read mutation should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-handle-read dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-handle-read mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-handle-read summary should use restored names");
            check(!summary.renamed,
                "valid post-handle-read summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-handle-read summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-handle-read summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all handle-read recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after handle reads");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after handle reads");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after handle reads");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-first",
        "source package should not contain the saved handle-read materialized value");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-second",
        "source package should not contain the later handle-read materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first handle-read recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientHandleReads",
        "first handle-read recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-first",
        "first output should contain the saved value before handle reads");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-second",
        "first output should not contain the later post-handle-read mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second handle-read recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientHandleReads",
        "second handle-read recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-first",
        "second output should preserve the saved value after handle reads");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-handle-reads-second",
        "second output should include the valid post-handle-read mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after handle reads");

    check_reopened_guard_recovery_materialized_output(
        second_output,
        options,
        "handle-read recovery",
        "rename-back-handle-reads-first",
        "rename-back-handle-reads-second");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "handle-read no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == 4,
        "handle-read no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "handle-read no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "handle-read no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "handle-read no-op save");
    check(!editor.last_edit_error().has_value(),
        "handle-read no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "handle-read no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "handle-read no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "handle-read no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "handle-read no-op save should leave the source package unchanged");

    check_reopened_guard_recovery_materialized_output(
        noop_output,
        options,
        "handle-read no-op",
        "rename-back-handle-reads-first",
        "rename-back-handle-reads-second");
}

void test_public_worksheet_editor_rename_back_failed_save_as_invalid_reads_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-reads-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-reads-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-reads-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-reads-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientInvalidReads");
    editor.rename_sheet("TransientInvalidReads", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-invalid-reads-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before invalid-read recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before invalid reads should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired invalid-read handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const fastxlsx::CellValue saved_before_invalid_reads = reacquired.get_cell(1, 1);
    check(saved_before_invalid_reads.kind() == fastxlsx::CellValueKind::Text &&
            saved_before_invalid_reads.text_value() == "rename-back-invalid-reads-first",
        "reacquired invalid-read setup should expose the saved materialized value");
    check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Text &&
            sheet.get_cell("A2").text_value() == "placeholder-a2",
        "invalid-read setup should preserve unchanged source-backed cells");

    const std::size_t baseline_count = reacquired.cell_count();
    const std::size_t baseline_memory = reacquired.estimated_memory_usage();
    check(baseline_count == 3 && baseline_memory > 0,
        "invalid-read setup should start from the saved sparse store");
    check(!editor.last_edit_error().has_value(),
        "invalid-read setup should start without mutation diagnostics");

    check(threw_fastxlsx_error([&] { (void)reacquired.try_cell(0, 1); }),
        "post-recovery invalid read should reject row zero");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "post-recovery invalid read should reject column zero");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(1048577, 1); }),
        "post-recovery invalid read should reject rows beyond Excel limits");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(1, 16385); }),
        "post-recovery invalid read should reject columns beyond Excel limits");
    check(threw_fastxlsx_error([&] { (void)reacquired.try_cell("a1"); }),
        "post-recovery invalid A1 read should reject lowercase references");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("A1:B2"); }),
        "post-recovery invalid A1 read should reject range references");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("A01"); }),
        "post-recovery invalid A1 read should reject leading-zero rows");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell("XFE1"); }),
        "post-recovery invalid A1 read should reject columns beyond Excel limits");
    check(threw_fastxlsx_error([&] {
        (void)reacquired.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "post-recovery invalid range read should reject row zero");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {2, 1, 1, 1});
    }), "post-recovery invalid range read should reject reversed ranges");

    check(reacquired.cell_count() == baseline_count &&
            sheet.cell_count() == baseline_count,
        "post-recovery invalid reads should not mutate sparse cell counts");
    check(reacquired.estimated_memory_usage() == baseline_memory &&
            sheet.estimated_memory_usage() == baseline_memory,
        "post-recovery invalid reads should not change sparse memory estimates");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-invalid-reads-first",
        "TransientInvalidReads",
        "post-recovery invalid reads",
        3);

    const fastxlsx::CellValue unchanged_after_invalid_reads = sheet.get_cell("A2");
    check(unchanged_after_invalid_reads.kind() == fastxlsx::CellValueKind::Text &&
            unchanged_after_invalid_reads.text_value() == "placeholder-a2",
        "post-recovery invalid reads should preserve unchanged source-backed cells");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after invalid reads should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-invalid-reads-first",
        "matching reacquire after invalid reads should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-invalid-reads-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-invalid-read mutation should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-invalid-read dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-invalid-read mutation should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-invalid-read summary should use restored names");
            check(!summary.renamed,
                "valid post-invalid-read summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-invalid-read summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-invalid-read summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all invalid-read recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after invalid reads");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after invalid reads");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after invalid reads");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-first",
        "source package should not contain the saved invalid-read materialized value");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-second",
        "source package should not contain the later invalid-read materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first invalid-read recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientInvalidReads",
        "first invalid-read recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-first",
        "first output should contain the saved value before invalid reads");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-second",
        "first output should not contain the later post-invalid-read mutation");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second invalid-read recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientInvalidReads",
        "second invalid-read recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-first",
        "second output should preserve the saved value after invalid reads");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-reads-second",
        "second output should include the valid post-invalid-read mutation");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after invalid reads");

    check_reopened_guard_recovery_materialized_output(
        second_output,
        options,
        "invalid-read recovery",
        "rename-back-invalid-reads-first",
        "rename-back-invalid-reads-second");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "invalid-read no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == 4,
        "invalid-read no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "invalid-read no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "invalid-read no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "invalid-read no-op save");
    check(!editor.last_edit_error().has_value(),
        "invalid-read no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "invalid-read no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "invalid-read no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "invalid-read no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid-read no-op save should leave the source package unchanged");

    check_reopened_guard_recovery_materialized_output(
        noop_output,
        options,
        "invalid-read no-op",
        "rename-back-invalid-reads-first",
        "rename-back-invalid-reads-second");
}

void test_public_worksheet_editor_rename_back_failed_save_as_invalid_mutations_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-mutations-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-mutations-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-mutations-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-invalid-mutations-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientInvalidMutations");
    editor.rename_sheet("TransientInvalidMutations", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-invalid-mutations-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before invalid-mutation recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before invalid mutations should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired invalid-mutation handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const fastxlsx::CellValue saved_before_invalid_mutations =
        reacquired.get_cell(1, 1);
    check(saved_before_invalid_mutations.kind() == fastxlsx::CellValueKind::Text &&
            saved_before_invalid_mutations.text_value() ==
                "rename-back-invalid-mutations-first",
        "reacquired invalid-mutation setup should expose the saved materialized value");
    const fastxlsx::CellValue source_backed_before_invalid_mutations =
        sheet.get_cell("A2");
    check(source_backed_before_invalid_mutations.kind() ==
                fastxlsx::CellValueKind::Text &&
            source_backed_before_invalid_mutations.text_value() == "placeholder-a2",
        "invalid-mutation setup should preserve unchanged source-backed cells");

    const std::size_t baseline_count = reacquired.cell_count();
    const std::size_t baseline_memory = reacquired.estimated_memory_usage();
    check(baseline_count == 3 && baseline_memory > 0,
        "invalid-mutation setup should start from the saved sparse store");
    check(!editor.last_edit_error().has_value(),
        "invalid-mutation setup should start without mutation diagnostics");

    check(threw_fastxlsx_error([&] {
        reacquired.set_cell(0, 1,
            fastxlsx::CellValue::text("invalid-mutation-row-zero"));
    }), "post-recovery invalid mutation should reject row zero");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("a1",
            fastxlsx::CellValue::text("invalid-mutation-lowercase"));
    }), "post-recovery invalid mutation should reject lowercase references");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("XFE1",
            fastxlsx::CellValue::text("invalid-mutation-column-overflow"));
    }), "post-recovery invalid mutation should reject overflow columns");
    check(threw_fastxlsx_error([&] { sheet.erase_cell(1048577, 1); }),
        "post-recovery invalid erase should reject rows beyond Excel limits");
    check(threw_fastxlsx_error([&] { reacquired.erase_cell("A1:B2"); }),
        "post-recovery invalid erase should reject range references");

    const std::optional<std::string> invalid_mutation_error = editor.last_edit_error();
    check(invalid_mutation_error.has_value(),
        "post-recovery invalid mutations should update last_edit_error");
    check(reacquired.cell_count() == baseline_count &&
            sheet.cell_count() == baseline_count,
        "post-recovery invalid mutations should not mutate sparse cell counts");
    check(reacquired.estimated_memory_usage() == baseline_memory &&
            sheet.estimated_memory_usage() == baseline_memory,
        "post-recovery invalid mutations should not change sparse memory estimates");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-invalid-mutations-first",
        "TransientInvalidMutations",
        "post-recovery invalid mutations",
        3,
        invalid_mutation_error);

    const fastxlsx::CellValue unchanged_after_invalid_mutations = sheet.get_cell("A2");
    check(unchanged_after_invalid_mutations.kind() == fastxlsx::CellValueKind::Text &&
            unchanged_after_invalid_mutations.text_value() == "placeholder-a2",
        "post-recovery invalid mutations should preserve unchanged source-backed cells");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after invalid mutations should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-invalid-mutations-first",
        "matching reacquire after invalid mutations should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-invalid-mutations-second"));
    check(!editor.last_edit_error().has_value(),
        "valid post-invalid-mutation edit should clear mutation diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-invalid-mutation edit should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-invalid-mutation dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-invalid-mutation edit should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-invalid-mutation summary should use restored names");
            check(!summary.renamed,
                "valid post-invalid-mutation summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-invalid-mutation summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-invalid-mutation summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all invalid-mutation recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after invalid mutations");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after invalid mutations");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after invalid mutations");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-first",
        "source package should not contain the saved invalid-mutation materialized value");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-second",
        "source package should not contain the later invalid-mutation materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first invalid-mutation recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientInvalidMutations",
        "first invalid-mutation recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-first",
        "first output should contain the saved value before invalid mutations");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-second",
        "first output should not contain the later post-invalid-mutation edit");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-row-zero",
        "first output should not contain rejected invalid row payload");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-lowercase",
        "first output should not contain rejected invalid A1 payload");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-column-overflow",
        "first output should not contain rejected invalid column payload");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second invalid-mutation recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientInvalidMutations",
        "second invalid-mutation recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-first",
        "second output should preserve the saved value after invalid mutations");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-invalid-mutations-second",
        "second output should include the valid post-invalid-mutation edit");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-row-zero",
        "second output should not contain rejected invalid row payload");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-lowercase",
        "second output should not contain rejected invalid A1 payload");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "invalid-mutation-column-overflow",
        "second output should not contain rejected invalid column payload");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after invalid mutations");

    check_reopened_guard_recovery_materialized_output(
        second_output,
        options,
        "invalid-mutation recovery",
        "rename-back-invalid-mutations-first",
        "rename-back-invalid-mutations-second");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "invalid-mutation no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == 4,
        "invalid-mutation no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "invalid-mutation no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "invalid-mutation no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "invalid-mutation no-op save");
    check(!editor.last_edit_error().has_value(),
        "invalid-mutation no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "invalid-mutation no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "invalid-mutation no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "invalid-mutation no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid-mutation no-op save should leave the source package unchanged");

    check_reopened_guard_recovery_materialized_output(
        noop_output,
        options,
        "invalid-mutation no-op",
        "rename-back-invalid-mutations-first",
        "rename-back-invalid-mutations-second");
}

void test_public_worksheet_editor_rename_back_failed_save_as_shift_guards_preserve_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-shift-guards-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-shift-guards-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-shift-guards-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-shift-guards-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientShiftGuards");
    editor.rename_sheet("TransientShiftGuards", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-shift-guards-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before shift-guard recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before shift guards should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired shift-guard handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const std::size_t baseline_count = reacquired.cell_count();
    const std::size_t baseline_memory = reacquired.estimated_memory_usage();
    check(baseline_count == 3 && baseline_memory > 0,
        "shift-guard setup should start from the saved sparse store");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("shift-guard-invalid-lowercase"));
    }), "invalid mutation should seed last_edit_error before shift guard no-ops");
    check(editor.last_edit_error().has_value(),
        "invalid mutation before shift guard no-ops should update last_edit_error");

    reacquired.insert_rows(2, 0);
    sheet.delete_rows(2, 0);
    reacquired.insert_columns(10, 1);
    sheet.delete_columns(10, 1);

    check(!editor.last_edit_error().has_value(),
        "post-recovery shift no-ops should clear prior mutation diagnostics");
    check(reacquired.cell_count() == baseline_count &&
            sheet.cell_count() == baseline_count,
        "post-recovery shift no-ops should not mutate sparse cell counts");
    check(reacquired.estimated_memory_usage() == baseline_memory &&
            sheet.estimated_memory_usage() == baseline_memory,
        "post-recovery shift no-ops should not change sparse memory estimates");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-shift-guards-first",
        "TransientShiftGuards",
        "post-recovery shift no-ops",
        3);

    check(threw_fastxlsx_error([&] { reacquired.insert_rows(0, 1); }),
        "post-recovery insert_rows should reject row zero");
    check(threw_fastxlsx_error([&] { sheet.delete_columns(16384, 2); }),
        "post-recovery delete_columns should reject count ranges past the Excel column limit");

    const std::optional<std::string> shift_error = editor.last_edit_error();
    check(shift_error.has_value(),
        "post-recovery invalid shifts should update last_edit_error");
    if (shift_error.has_value()) {
        check_contains(*shift_error, "16384",
            "post-recovery invalid shift diagnostic should expose the Excel column limit");
    }
    check(reacquired.cell_count() == baseline_count &&
            sheet.cell_count() == baseline_count,
        "post-recovery invalid shifts should preserve sparse cell counts");
    check(reacquired.estimated_memory_usage() == baseline_memory &&
            sheet.estimated_memory_usage() == baseline_memory,
        "post-recovery invalid shifts should preserve sparse memory estimates");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-shift-guards-first",
        "TransientShiftGuards",
        "post-recovery invalid shift guards",
        3,
        shift_error);

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after invalid shifts should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-shift-guards-first",
        "matching reacquire after invalid shifts should still use saved state");

    matching.insert_rows(2, 1);
    check(!editor.last_edit_error().has_value(),
        "valid post-invalid-shift edit should clear shift diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-invalid-shift edit should dirty shared handles");
    check(!matching.try_cell("A2").has_value(),
        "valid post-invalid-shift row insertion should remove the old source-backed coordinate");
    check(matching.get_cell("A3").text_value() == "placeholder-a2",
        "valid post-invalid-shift row insertion should shift source-backed rows");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-invalid-shift dirty diagnostics should use restored source name");
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all shift-guard recovery handles");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after shift guards");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after shift guards");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-shift-guards-first",
        "source package should not contain the saved shift-guard materialized value");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "shift-guard-invalid-lowercase",
        "source package should not contain rejected invalid shift-guard payload");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first shift-guard recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientShiftGuards",
        "first shift-guard recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-shift-guards-first",
        "first output should contain the saved value before shift guards");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "shift-guard-invalid-lowercase",
        "first output should not contain rejected invalid mutation payload");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second shift-guard recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientShiftGuards",
        "second shift-guard recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-shift-guards-first",
        "second output should preserve the saved value after shift guards");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A3" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "second output should persist the valid post-invalid-shift row insertion");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "shift-guard-invalid-lowercase",
        "second output should not contain rejected invalid mutation payload");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A2" t="inlineStr"><is><t>placeholder-a2</t></is></c>)",
        "second output should not keep the old shifted source-backed coordinate");

    fastxlsx::WorkbookEditor reopened_editor =
        fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);
    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        "reopened shift-guard output should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0,
        "reopened shift-guard output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 3,
        "reopened shift-guard output should keep the saved sparse cell count");
    check(reopened_sheet.get_cell("A1").text_value() ==
            "rename-back-shift-guards-first",
        "reopened shift-guard output should read back preserved row-one text");
    check(!reopened_sheet.try_cell("A2").has_value(),
        "reopened shift-guard output should not read back the old source-backed coordinate");
    const fastxlsx::CellValue reopened_shifted_source =
        reopened_sheet.get_cell("A3");
    check(reopened_shifted_source.kind() == fastxlsx::CellValueKind::Text &&
            reopened_shifted_source.text_value() == "placeholder-a2",
        "reopened shift-guard output should read back shifted source-backed row");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
        reopened_sheet.row_cells(3);
    check(reopened_row_three.size() == 1 &&
            reopened_row_three[0].reference.row == 3 &&
            reopened_row_three[0].reference.column == 1 &&
            reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_row_three[0].value.text_value() == "placeholder-a2",
        "reopened shift-guard row_cells should expose the shifted source-backed cell");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "shift-guard no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == save_state_before_noop.pending_change_count,
        "shift-guard no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift-guard no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "shift-guard no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift-guard no-op save");
    check(!editor.last_edit_error().has_value(),
        "shift-guard no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "shift-guard no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "shift-guard no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "shift-guard no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift-guard no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_editor =
        fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor noop_sheet =
        noop_editor.worksheet("Data", options);
    check(!noop_editor.has_pending_changes() && !noop_sheet.has_pending_changes(),
        "shift-guard no-op reopened output should start clean");
    check(noop_editor.pending_change_count() == 0 &&
            noop_editor.pending_materialized_cell_count() == 0,
        "shift-guard no-op reopened output should not expose dirty diagnostics");
    check(noop_sheet.cell_count() == 3,
        "shift-guard no-op reopened output should keep the saved sparse cell count");
    check(noop_sheet.get_cell("A1").text_value() ==
            "rename-back-shift-guards-first",
        "shift-guard no-op reopened output should read back preserved row-one text");
    check(!noop_sheet.try_cell("A2").has_value(),
        "shift-guard no-op reopened output should not read back the old source-backed coordinate");
    const fastxlsx::CellValue noop_shifted_source =
        noop_sheet.get_cell("A3");
    check(noop_shifted_source.kind() == fastxlsx::CellValueKind::Text &&
            noop_shifted_source.text_value() == "placeholder-a2",
        "shift-guard no-op reopened output should read back shifted source-backed row");
    const std::vector<fastxlsx::WorksheetCellSnapshot> noop_row_three =
        noop_sheet.row_cells(3);
    check(noop_row_three.size() == 1 &&
            noop_row_three[0].reference.row == 3 &&
            noop_row_three[0].reference.column == 1 &&
            noop_row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            noop_row_three[0].value.text_value() == "placeholder-a2",
        "shift-guard no-op reopened row_cells should expose the shifted source-backed cell");
}

void test_public_worksheet_editor_rename_back_failed_save_as_missing_erase_preserves_reacquired_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-erase-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-erase-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-erase-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-missing-erase-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientMissingErase");
    editor.rename_sheet("TransientMissingErase", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-missing-erase-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before missing-erase recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before missing erase should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired missing-erase handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const fastxlsx::CellValue saved_before_missing_erase = reacquired.get_cell(1, 1);
    check(saved_before_missing_erase.kind() == fastxlsx::CellValueKind::Text &&
            saved_before_missing_erase.text_value() == "rename-back-missing-erase-first",
        "reacquired missing-erase setup should expose the saved materialized value");
    const fastxlsx::CellValue source_backed_before_missing_erase =
        sheet.get_cell("A2");
    check(source_backed_before_missing_erase.kind() == fastxlsx::CellValueKind::Text &&
            source_backed_before_missing_erase.text_value() == "placeholder-a2",
        "missing-erase setup should preserve unchanged source-backed cells");

    const std::size_t baseline_count = reacquired.cell_count();
    const std::size_t baseline_memory = reacquired.estimated_memory_usage();
    check(baseline_count == 3 && baseline_memory > 0,
        "missing-erase setup should start from the saved sparse store");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("missing-erase-invalid"));
    }), "invalid mutation should seed last_edit_error before missing erase");
    check(editor.last_edit_error().has_value(),
        "invalid mutation before missing erase should update last_edit_error");

    reacquired.erase_cell(9, 9);
    sheet.erase_cell("D4");

    check(!editor.last_edit_error().has_value(),
        "post-recovery missing erase no-op should clear prior mutation diagnostics");
    check(reacquired.cell_count() == baseline_count &&
            sheet.cell_count() == baseline_count,
        "post-recovery missing erase no-op should not mutate sparse cell counts");
    check(reacquired.estimated_memory_usage() == baseline_memory &&
            sheet.estimated_memory_usage() == baseline_memory,
        "post-recovery missing erase no-op should not change sparse memory estimates");

    check_public_saved_materialized_recovery_clean_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "rename-back-missing-erase-first",
        "TransientMissingErase",
        "post-recovery missing erase no-op",
        3);

    const fastxlsx::CellValue unchanged_after_missing_erase = sheet.get_cell("A2");
    check(unchanged_after_missing_erase.kind() == fastxlsx::CellValueKind::Text &&
            unchanged_after_missing_erase.text_value() == "placeholder-a2",
        "post-recovery missing erase no-op should preserve unchanged source-backed cells");

    fastxlsx::WorksheetEditor matching = editor.worksheet("Data", options);
    check(!matching.has_pending_changes(),
        "matching reacquire after missing erase should remain clean");
    const fastxlsx::CellValue matching_value = matching.get_cell(1, 1);
    check(matching_value.kind() == fastxlsx::CellValueKind::Text &&
            matching_value.text_value() == "rename-back-missing-erase-first",
        "matching reacquire after missing erase should still use saved state");

    matching.set_cell(2, 2,
        fastxlsx::CellValue::text("rename-back-missing-erase-second"));
    check(sheet.has_pending_changes() && reacquired.has_pending_changes() &&
            matching.has_pending_changes(),
        "valid post-missing-erase edit should dirty shared handles");
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "valid post-missing-erase dirty diagnostics should use restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "valid post-missing-erase edit should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "valid post-missing-erase summary should use restored names");
            check(!summary.renamed,
                "valid post-missing-erase summary should not be marked renamed");
            check(summary.materialized_dirty,
                "valid post-missing-erase summary should report dirty materialized state");
            check(!summary.sheet_data_replaced,
                "valid post-missing-erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "second safe save_as should clean all missing-erase recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the later materialized handoff after missing erase");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after missing erase");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain the original value after missing erase");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-first",
        "source package should not contain the saved missing-erase materialized value");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-second",
        "source package should not contain the later missing-erase materialized value");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first missing-erase recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientMissingErase",
        "first missing-erase recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-first",
        "first output should contain the saved value before missing erase");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-second",
        "first output should not contain the later post-missing-erase edit");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "missing-erase-invalid",
        "first output should not contain rejected invalid mutation payload");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/workbook.xml"), R"(name="Data")",
        "second missing-erase recovery output should keep the restored source name");
    check_not_contains(second_entries.at("xl/workbook.xml"), "TransientMissingErase",
        "second missing-erase recovery output should not leak the transient planned name");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-first",
        "second output should preserve the saved value after missing erase");
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-missing-erase-second",
        "second output should include the valid post-missing-erase edit");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"),
        "missing-erase-invalid",
        "second output should not contain rejected invalid mutation payload");
    check_not_contains(second_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "second output should not reload stale source A1 after missing erase");

    check_reopened_guard_recovery_materialized_output(
        second_output,
        options,
        "missing-erase recovery",
        "rename-back-missing-erase-first",
        "rename-back-missing-erase-second");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes() &&
            !matching.has_pending_changes(),
        "missing-erase no-op save should keep recovery handles clean");
    check(editor.pending_change_count() == 4,
        "missing-erase no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "missing-erase no-op save should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "missing-erase no-op save should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "missing-erase no-op save");
    check(!editor.last_edit_error().has_value(),
        "missing-erase no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "missing-erase no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "missing-erase no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "missing-erase no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "missing-erase no-op save should leave the source package unchanged");

    check_reopened_guard_recovery_materialized_output(
        noop_output,
        options,
        "missing-erase no-op",
        "rename-back-missing-erase-first",
        "rename-back-missing-erase-second");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = workbook_editor_shard_from_args(argc, argv);
        std::printf("fastxlsx.workbook_editor shard: %.*s\n",
            static_cast<int>(shard.size()), shard.data());

        if (should_run_workbook_editor_shard(shard, "public-retry-guards")) {
            test_public_worksheet_editor_rename_back_failed_save_as_handle_reads_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_invalid_reads_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_invalid_mutations_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_shift_guards_preserve_reacquired_state();
            test_public_worksheet_editor_rename_back_failed_save_as_missing_erase_preserves_reacquired_state();
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
