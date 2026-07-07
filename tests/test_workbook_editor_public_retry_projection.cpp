#include "test_workbook_editor_public_retry_common.hpp"

void check_reopened_blank_erase_projection(
    const std::filesystem::path& output,
    const fastxlsx::WorksheetEditorOptions& options)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        "reopened blank/erase projection should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0,
        "reopened blank/erase projection should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 2,
        "reopened blank/erase projection should keep blank A1 and source-backed B1");

    const fastxlsx::CellValue reopened_blank = reopened_sheet.get_cell("A1");
    check(reopened_blank.kind() == fastxlsx::CellValueKind::Blank,
        "reopened blank/erase projection should read A1 as an explicit blank");
    const fastxlsx::CellValue reopened_number = reopened_sheet.get_cell("B1");
    check(reopened_number.kind() == fastxlsx::CellValueKind::Number &&
            reopened_number.number_value() == 1.0,
        "reopened blank/erase projection should preserve source-backed B1");
    check(!reopened_sheet.try_cell("A2").has_value(),
        "reopened blank/erase projection should not read erased A2");

    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
        reopened_sheet.row_cells(1);
    check(reopened_row_one.size() == 2 &&
            reopened_row_one[0].reference.row == 1 &&
            reopened_row_one[0].reference.column == 1 &&
            reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Blank &&
            reopened_row_one[1].reference.row == 1 &&
            reopened_row_one[1].reference.column == 2 &&
            reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_row_one[1].value.number_value() == 1.0,
        "reopened blank/erase row_cells should expose the blank and source number");
}

void check_reopened_scalar_formula_projection(
    const std::filesystem::path& output,
    const fastxlsx::WorksheetEditorOptions& options)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        "reopened scalar/formula projection should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0,
        "reopened scalar/formula projection should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 4,
        "reopened scalar/formula projection should keep A1, B1, A2, and C3");

    const fastxlsx::CellValue reopened_number = reopened_sheet.get_cell("A1");
    const fastxlsx::CellValue reopened_source = reopened_sheet.get_cell("B1");
    const fastxlsx::CellValue reopened_boolean = reopened_sheet.get_cell("A2");
    const fastxlsx::CellValue reopened_formula = reopened_sheet.get_cell("C3");
    check(reopened_number.kind() == fastxlsx::CellValueKind::Number &&
            reopened_number.number_value() == 42.25,
        "reopened scalar/formula projection should read numeric A1");
    check(reopened_source.kind() == fastxlsx::CellValueKind::Number &&
            reopened_source.number_value() == 1.0,
        "reopened scalar/formula projection should preserve source-backed B1");
    check(reopened_boolean.kind() == fastxlsx::CellValueKind::Boolean &&
            reopened_boolean.boolean_value(),
        "reopened scalar/formula projection should read boolean A2");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == R"(SUM(A1:B1)&"<ok>")",
        "reopened scalar/formula projection should read formula C3");
}

void check_reopened_text_escape_projection(
    const std::filesystem::path& output,
    const fastxlsx::WorksheetEditorOptions& options)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        "reopened text escape projection should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0,
        "reopened text escape projection should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 4,
        "reopened text escape projection should keep A1, B1, A2, and C3");

    const fastxlsx::CellValue reopened_whitespace = reopened_sheet.get_cell("A1");
    const fastxlsx::CellValue reopened_source = reopened_sheet.get_cell("B1");
    const fastxlsx::CellValue reopened_empty = reopened_sheet.get_cell("A2");
    const fastxlsx::CellValue reopened_special = reopened_sheet.get_cell("C3");
    check(reopened_whitespace.kind() == fastxlsx::CellValueKind::Text &&
            reopened_whitespace.text_value() == R"(  A&B <C> "D"  )",
        "reopened text escape projection should preserve whitespace text");
    check(reopened_source.kind() == fastxlsx::CellValueKind::Number &&
            reopened_source.number_value() == 1.0,
        "reopened text escape projection should preserve source-backed B1");
    check(reopened_empty.kind() == fastxlsx::CellValueKind::Text &&
            reopened_empty.text_value().empty(),
        "reopened text escape projection should read empty A2 text");
    check(reopened_special.kind() == fastxlsx::CellValueKind::Text &&
            reopened_special.text_value() == R"(A&B <C> > "Q")",
        "reopened text escape projection should read special-character C3 text");
}

void check_clean_noop_projection_save(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& first_handle,
    fastxlsx::WorksheetEditor& second_handle,
    const std::filesystem::path& noop_output,
    const std::filesystem::path& source,
    const std::map<std::string, std::string>& source_entries,
    const std::map<std::string, std::string>& expected_entries,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);

    check(!first_handle.has_pending_changes() && !second_handle.has_pending_changes(),
        prefix + " should keep recovery handles clean");
    check(editor.pending_change_count() == save_state_before_noop.pending_change_count,
        prefix + " should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should keep edit summaries empty");
    check_workbook_editor_no_replacement_diagnostics(editor, prefix);
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, prefix);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, prefix);
    check(fastxlsx::test::read_zip_entries(noop_output) == expected_entries,
        prefix + " output should match the saved projection");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " should leave the source package unchanged");
}

void test_public_worksheet_editor_rename_back_failed_save_as_blank_and_existing_erase_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-blank-erase-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-blank-erase-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-blank-erase-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-blank-erase-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientBlankErase");
    editor.rename_sheet("TransientBlankErase", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-blank-erase-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before blank/erase recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before blank/erase should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired blank/erase handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };

    const fastxlsx::CellValue saved_before_blank_erase = reacquired.get_cell(1, 1);
    check(saved_before_blank_erase.kind() == fastxlsx::CellValueKind::Text &&
            saved_before_blank_erase.text_value() == "rename-back-blank-erase-first",
        "reacquired blank/erase setup should expose the saved materialized value");
    const fastxlsx::CellValue source_backed_before_blank_erase = sheet.get_cell("A2");
    check(source_backed_before_blank_erase.kind() == fastxlsx::CellValueKind::Text &&
            source_backed_before_blank_erase.text_value() == "placeholder-a2",
        "blank/erase setup should preserve unchanged source-backed cells");
    check(!editor.last_edit_error().has_value(),
        "blank/erase setup should start without edit diagnostics");

    reacquired.set_cell("A1", fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);

    check(!editor.last_edit_error().has_value(),
        "post-recovery blank/erase mutations should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "post-recovery blank/erase mutations should dirty shared handles");
    check(reacquired.cell_count() == 2 && sheet.cell_count() == 2,
        "post-recovery blank/erase mutations should keep blank A1 and B1 only");
    {
        const fastxlsx::CellValue blank_a1 = sheet.get_cell("A1");
        check(blank_a1.kind() == fastxlsx::CellValueKind::Blank,
            "post-recovery explicit blank should be readable as CellValue::blank");
        check(!reacquired.try_cell(2, 1).has_value(),
            "post-recovery existing erase should remove source-backed A2");
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientBlankErase",
        "post-recovery blank/erase mutations",
        3,
        2,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean blank/erase recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the blank/erase materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear blank/erase dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear blank/erase dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear blank/erase dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after blank/erase");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after blank/erase");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "source package should still contain original A2 after blank/erase");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first blank/erase recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientBlankErase",
        "first blank/erase recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-blank-erase-first",
        "first output should contain the saved value before blank/erase");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "first output should still contain source-backed A2 before erase");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "first output should not reload stale source A1");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second blank/erase recovery output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientBlankErase",
        "second blank/erase recovery output should not leak the transient planned name");
    check_contains(second_worksheet_xml,
        R"(<row r="1"><c r="A1"/><c r="B1"><v>1</v></c></row>)",
        "second output should persist explicit blank A1 and preserve B1");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "second output should refresh dimension after existing source-cell erase");
    check_not_contains(second_worksheet_xml, "rename-back-blank-erase-first",
        "second output should remove prior text payload after explicit blank");
    check_not_contains(second_worksheet_xml, "placeholder-a1",
        "second output should not reload stale source A1 after blank");
    check_not_contains(second_worksheet_xml, "placeholder-a2",
        "second output should remove erased source-backed A2");
    check_not_contains(second_worksheet_xml, R"(<row r="2")",
        "second output should remove row 2 after erasing its only source cell");

    check_reopened_blank_erase_projection(second_output, options);
    check_clean_noop_projection_save(
        editor,
        sheet,
        reacquired,
        noop_output,
        source,
        source_entries,
        second_entries,
        "blank/erase projection no-op save");
    check_reopened_blank_erase_projection(noop_output, options);
}

void test_public_worksheet_editor_rename_back_failed_save_as_scalar_and_formula_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-scalar-formula-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-scalar-formula-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-scalar-formula-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-scalar-formula-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientScalarFormula");
    editor.rename_sheet("TransientScalarFormula", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("rename-back-scalar-formula-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before scalar/formula recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before scalar/formula edits should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired scalar/formula handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };
    check(!editor.last_edit_error().has_value(),
        "scalar/formula setup should start without edit diagnostics");
    {
        const fastxlsx::CellValue saved_a1 = sheet.get_cell("A1");
        check(saved_a1.kind() == fastxlsx::CellValueKind::Text &&
                saved_a1.text_value() == "rename-back-scalar-formula-first",
            "scalar/formula setup should expose the saved materialized A1 value");
        const fastxlsx::CellValue preserved_a2 = reacquired.get_cell(2, 1);
        check(preserved_a2.kind() == fastxlsx::CellValueKind::Text &&
                preserved_a2.text_value() == "placeholder-a2",
            "scalar/formula setup should preserve unchanged source-backed A2");
    }

    reacquired.set_cell(1, 1, fastxlsx::CellValue::number(42.25));
    sheet.set_cell("A2", fastxlsx::CellValue::boolean(true));
    reacquired.set_cell(3, 3,
        fastxlsx::CellValue::formula(R"(SUM(A1:B1)&"<ok>")"));

    check(!editor.last_edit_error().has_value(),
        "post-recovery scalar/formula mutations should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "post-recovery scalar/formula mutations should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "post-recovery scalar/formula mutations should keep A1, B1, A2, and C3");
    {
        const fastxlsx::CellValue number_a1 = sheet.get_cell("A1");
        const fastxlsx::CellValue preserved_b1 = reacquired.get_cell(1, 2);
        const fastxlsx::CellValue boolean_a2 = reacquired.get_cell("A2");
        const fastxlsx::CellValue formula_c3 = sheet.get_cell(3, 3);
        check(number_a1.kind() == fastxlsx::CellValueKind::Number &&
                number_a1.number_value() == 42.25,
            "post-recovery scalar/formula edits should read A1 as a number");
        check(preserved_b1.kind() == fastxlsx::CellValueKind::Number &&
                preserved_b1.number_value() == 1.0,
            "post-recovery scalar/formula edits should preserve source-backed B1");
        check(boolean_a2.kind() == fastxlsx::CellValueKind::Boolean &&
                boolean_a2.boolean_value(),
            "post-recovery scalar/formula edits should read A2 as true");
        check(formula_c3.kind() == fastxlsx::CellValueKind::Formula &&
                formula_c3.text_value() == R"(SUM(A1:B1)&"<ok>")",
            "post-recovery scalar/formula edits should read C3 formula text");
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientScalarFormula",
        "post-recovery scalar/formula mutations",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean scalar/formula recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the scalar/formula materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear scalar/formula dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear scalar/formula dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear scalar/formula dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after scalar/formula edits");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after scalar/formula edits");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "source package should still contain original A2 after scalar/formula edits");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "42.25",
        "source package should not contain later scalar edits");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "SUM(A1:B1)",
        "source package should not contain later formula edits");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first scalar/formula recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientScalarFormula",
        "first scalar/formula recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-scalar-formula-first",
        "first output should contain the saved value before scalar/formula edits");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "first output should still contain source-backed A2 before boolean replacement");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "42.25",
        "first output should not contain later number replacement");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "SUM(A1:B1)",
        "first output should not contain later formula replacement");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second scalar/formula recovery output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientScalarFormula",
        "second scalar/formula recovery output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "second scalar/formula output should refresh dimension through C3");
    check_contains(second_worksheet_xml, R"(<c r="A1"><v>42.25</v></c>)",
        "second output should persist numeric A1 replacement as scalar value");
    check_contains(second_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "second output should preserve source-backed B1 number");
    check_contains(second_worksheet_xml, R"(<c r="A2" t="b"><v>1</v></c>)",
        "second output should persist boolean A2 replacement");
    check_contains(second_worksheet_xml,
        R"(<c r="C3"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f></c>)",
        "second output should persist escaped formula C3 without cached value");
    check_not_contains(second_worksheet_xml, "rename-back-scalar-formula-first",
        "second output should remove prior text payload after number replacement");
    check_not_contains(second_worksheet_xml, "placeholder-a1",
        "second output should not reload stale source A1 after number replacement");
    check_not_contains(second_worksheet_xml, "placeholder-a2",
        "second output should replace source-backed A2 with a boolean cell");

    check_reopened_scalar_formula_projection(second_output, options);
    check_clean_noop_projection_save(
        editor,
        sheet,
        reacquired,
        noop_output,
        source,
        source_entries,
        second_entries,
        "scalar/formula projection no-op save");
    check_reopened_scalar_formula_projection(noop_output, options);
}

void test_public_worksheet_editor_rename_back_failed_save_as_text_escape_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-text-escape-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-text-escape-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-text-escape-second.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-rename-back-failed-save-text-escape-noop.xlsx");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TransientTextEscape");
    editor.rename_sheet("TransientTextEscape", "Data");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    sheet.set_cell("A1",
        fastxlsx::CellValue::text("rename-back-text-escape-first"));

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "source-overwrite save_as should reject before text escape recovery setup flushes");
    editor.save_as(first_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data", options);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "matching reacquire before text escape edits should keep both handles clean");
    check(sheet.name() == "Data" && reacquired.name() == "Data",
        "saved and reacquired text escape handles should keep the restored planned name");
    const std::vector<std::string> expected_names = {"Data", "Untouched"};
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog = {
        {"Data", "Data", false},
        {"Untouched", "Untouched", false},
    };
    check(!editor.last_edit_error().has_value(),
        "text escape setup should start without edit diagnostics");
    {
        const fastxlsx::CellValue saved_a1 = sheet.get_cell(1, 1);
        check(saved_a1.kind() == fastxlsx::CellValueKind::Text &&
                saved_a1.text_value() == "rename-back-text-escape-first",
            "text escape setup should expose the saved materialized A1 value");
        const fastxlsx::CellValue preserved_a2 = reacquired.get_cell("A2");
        check(preserved_a2.kind() == fastxlsx::CellValueKind::Text &&
                preserved_a2.text_value() == "placeholder-a2",
            "text escape setup should preserve unchanged source-backed A2");
    }

    reacquired.set_cell("A1",
        fastxlsx::CellValue::text(R"(  A&B <C> "D"  )"));
    sheet.set_cell(2, 1, fastxlsx::CellValue::text(""));
    reacquired.set_cell(3, 3,
        fastxlsx::CellValue::text(R"(A&B <C> > "Q")"));

    check(!editor.last_edit_error().has_value(),
        "post-recovery text escape mutations should not create edit diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "post-recovery text escape mutations should dirty shared handles");
    check(sheet.cell_count() == 4 && reacquired.cell_count() == 4,
        "post-recovery text escape mutations should keep A1, B1, A2, and C3");
    {
        const fastxlsx::CellValue escaped_a1 = sheet.get_cell("A1");
        const fastxlsx::CellValue preserved_b1 = reacquired.get_cell("B1");
        const fastxlsx::CellValue empty_a2 = reacquired.get_cell(2, 1);
        const fastxlsx::CellValue escaped_c3 = sheet.get_cell("C3");
        check(escaped_a1.kind() == fastxlsx::CellValueKind::Text &&
                escaped_a1.text_value() == R"(  A&B <C> "D"  )",
            "post-recovery text escape edits should read back preserved whitespace text");
        check(preserved_b1.kind() == fastxlsx::CellValueKind::Number &&
                preserved_b1.number_value() == 1.0,
            "post-recovery text escape edits should preserve source-backed B1");
        check(empty_a2.kind() == fastxlsx::CellValueKind::Text &&
                empty_a2.text_value().empty(),
            "post-recovery text escape edits should read A2 as empty text");
        check(escaped_c3.kind() == fastxlsx::CellValueKind::Text &&
                escaped_c3.text_value() == R"(A&B <C> > "Q")",
            "post-recovery text escape edits should read C3 as special-character text");
    }
    check_public_dirty_materialized_recovery_state(
        editor,
        sheet,
        reacquired,
        expected_names,
        expected_names,
        expected_catalog,
        "TransientTextEscape",
        "post-recovery text escape mutations",
        3,
        4,
        sheet.estimated_memory_usage());

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "second safe save_as should clean text escape recovery handles");
    check(editor.pending_change_count() == 4,
        "second safe save_as should count the text escape materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second safe save_as should clear text escape dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "second safe save_as should clear text escape dirty cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "second safe save_as should clear text escape dirty memory");
    check(editor.pending_worksheet_edits().empty(),
        "second safe save_as should clear summaries after text escape edits");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "source package should still contain original A1 after text escape edits");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "source package should still contain original A2 after text escape edits");
    check_not_contains(source_entries.at("xl/worksheets/sheet1.xml"), "A&amp;B",
        "source package should not contain later escaped text edits");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="Data")",
        "first text escape recovery output should use the restored source name");
    check_not_contains(first_entries.at("xl/workbook.xml"), "TransientTextEscape",
        "first text escape recovery output should not leak the transient planned name");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        "rename-back-text-escape-first",
        "first output should contain the saved value before text escape edits");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a2",
        "first output should still contain source-backed A2 before empty replacement");
    check_not_contains(first_entries.at("xl/worksheets/sheet1.xml"), "A&amp;B",
        "first output should not contain later escaped text edits");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_workbook_xml, R"(name="Data")",
        "second text escape recovery output should keep the restored source name");
    check_not_contains(second_workbook_xml, "TransientTextEscape",
        "second text escape recovery output should not leak the transient planned name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "second text escape output should refresh dimension through C3");
    check_contains(second_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t xml:space="preserve">  A&amp;B &lt;C&gt; "D"  </t></is></c>)",
        "second output should persist whitespace text with xml:space and escaped text");
    check_contains(second_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "second output should preserve source-backed B1 number");
    check_contains(second_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t></t></is></c>)",
        "second output should persist empty text as an empty t element");
    check_contains(second_worksheet_xml,
        R"(<c r="C3" t="inlineStr"><is><t>A&amp;B &lt;C&gt; &gt; "Q"</t></is></c>)",
        "second output should persist special-character text without xml:space");
    check_not_contains(second_worksheet_xml, "rename-back-text-escape-first",
        "second output should remove prior text payload after whitespace replacement");
    check_not_contains(second_worksheet_xml, "placeholder-a1",
        "second output should not reload stale source A1 after text replacement");
    check_not_contains(second_worksheet_xml, "placeholder-a2",
        "second output should replace source-backed A2 with empty text");

    check_reopened_text_escape_projection(second_output, options);
    check_clean_noop_projection_save(
        editor,
        sheet,
        reacquired,
        noop_output,
        source,
        source_entries,
        second_entries,
        "text escape projection no-op save");
    check_reopened_text_escape_projection(noop_output, options);
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = workbook_editor_shard_from_args(argc, argv);
        std::printf("fastxlsx.workbook_editor shard: %.*s\n",
            static_cast<int>(shard.size()), shard.data());

        if (should_run_workbook_editor_shard(shard, "public-retry-projection")) {
            test_public_worksheet_editor_rename_back_failed_save_as_blank_and_existing_erase_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_scalar_and_formula_projection();
            test_public_worksheet_editor_rename_back_failed_save_as_text_escape_projection();
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
